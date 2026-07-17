# NexusRPC 总体详细设计

> 状态：开发基线
> 适用版本：v1.0 MVP、v1.1 治理能力版
> 依据：`Design docs/大纲.md`、`docs/development_decisions.md`

## 1. 文档定位

本文档是可执行工程说明，定义模块边界、运行时关系、公共接口方向、线程模型、生命周期和阶段验收。实现任务拆分见 `docs/tasks.md`，协议细节见 `docs/protocol.md`，MCP 行为见 `docs/mcp.md`，Redis 行为见 `docs/registry.md`，测试细节见 `docs/testing.md`。

未在本文档和决策文档中明确的功能不属于当前版本实现范围，不应由实现者自行扩展。

## 2. 版本边界

### 2.1 v1.0 MVP

目标是完成本机最短闭环：MCP stdio Client -> MCP Gateway -> RPC Client -> TCP -> RPC Server -> Weather Handler。

必须交付：

- 项目目录和 CMake target 骨架；
- `libnexus_net`：`EventLoop`、`Channel`、`TcpServer`、`TcpConnection`、`Buffer`；
- Unary RPC，固定 32 字节头，Big Endian，Protobuf body；
- RPC Server SDK 的 `registerService(serviceName, methodName, handler)`；
- RPC Client SDK 的同步调用和 future 包装；
- Weather、Echo 示例；
- MCP stdio：`initialize`、`initialized`、`ping`、`tools/list`、`tools/call`；
- 最小单元测试、端到端测试和 CI 基础配置。

v1.0 不实现 Redis、连接池、负载均衡、熔断、限流、Streamable HTTP、取消、真实进度和压缩。

### 2.2 v1.1

加入 Redis Registry、缓存和 Pub/Sub、连接池、RoundRobin、ConsistentHash、熔断、限流、重试、Streamable HTTP、Session、健康检查、Prometheus、集成测试和 Docker Compose 测试环境。

## 3. 部署与进程

### 3.1 NexusRPC Node

Node 是 Gateway 与 RPC Core 的单进程组合，默认承担：

- MCP Server；
- RPC Client；
- Registry Watcher；
- Tool Registry；
- 健康检查和指标 HTTP Server。

Gateway 调用 Core 使用进程内接口，不经过网络。`9600` 是 RPC Server 默认端口基准，不是 Gateway/Core 内部端口。Weather 使用 `9601`，Echo 使用 `9602`。Node 的 MCP HTTP 端口默认 `9800`，v1.0 stdio 模式可不监听 HTTP。

### 3.2 Backend Service

Backend 是独立进程，使用 RPC Server SDK 监听 TCP 端口并注册 handler。v1.0 可静态配置 Node 连接地址；v1.1 通过 Redis Registry 发现实例。Backend 不采用运行时 Proto 反射分发，业务代码显式注册 handler。

## 4. 目录与模块边界

```text
nexus-rpc/
├── CMakeLists.txt
├── README.md
├── cmake/
├── config/
├── docs/
├── include/nexus/
│   ├── net/                 # 网络公共接口
│   ├── rpc/                 # RPC 公共接口、状态、协议
│   ├── mcp/                 # JSON-RPC、Tool、MCP Server 接口
│   └── observability/       # 日志、指标、健康检查接口
├── src/
│   ├── net/                 # epoll、TCP、Buffer
│   ├── rpc/                 # 编解码、Client、Server、治理
│   ├── mcp/                 # JSON-RPC、Tool Registry、传输、Session
│   ├── registry/            # Registry 抽象与 Redis 实现
│   ├── observability/       # 日志、指标、健康检查
│   ├── pool/                # ThreadPool、MemoryPool
│   └── utils/               # Config、UUID、时间、Result
├── proto/
│   ├── nexus/rpc.proto
│   ├── nexus/options.proto
│   └── examples/weather.proto
├── examples/
│   ├── weather_service/
│   ├── echo_service/
│   └── mcp_gateway/
├── tests/unit/
├── tests/integration/
├── benchmarks/
├── scripts/
├── tools/
└── .github/workflows/
```

阶段 0 只创建目录、占位 README、CMake 子目录入口和文件清单，不拉取依赖、不验证编译环境、不实现业务代码。

## 5. 核心运行时

### 5.1 网络线程模型

- Main Reactor 只负责 accept；
- Sub Reactor 每线程一个 `EventLoop`，负责连接读写；
- 业务线程池执行 RPC handler；
- 连接以 Round-Robin 分配到 Sub Reactor；
- EventLoop 所有 Channel 修改必须在所属 EventLoop 线程执行；跨线程操作通过 `runInLoop`；
- ET 模式下读写回调必须循环处理直到 `EAGAIN`。

### 5.2 RPC Server 请求路径

1. TCP 连接读取 Buffer；
2. 按固定头长度判断帧是否完整；
3. 校验 magic、version、长度、压缩和序列化类型；
4. 解码 metadata 和 Protobuf body；
5. 根据 service/method 查找 handler；
6. 将 handler 投递到业务线程池；
7. 生成 RpcResponse 并回投 IO 线程；
8. 编码响应并写入连接。

### 5.3 Gateway 调用路径

1. 解析 JSON-RPC；
2. 校验 MCP Session 和方法参数；
3. 从 Tool Registry 查询工具；
4. 将 JSON 参数映射到请求 Protobuf；
5. 通过 RpcClient 调用目标实例；
6. 将响应 Protobuf 转为 Protobuf JSON Mapping；
7. 返回 `content[0].text` 和 `structuredContent`。

## 6. 公共接口约束

公共头文件只暴露稳定接口和数据类型，具体 epoll、Redis、线程同步实现放在 `src`。跨模块错误统一使用 `Status`/`Result<T>`，不得用异常作为 RPC 业务错误通道。handler 必须能返回状态和响应 body，禁止阻塞 IO 线程。

v1 Client 至少提供：

```cpp
class RpcClient {
public:
    Result<std::string> call(const RpcEndpoint&, const RpcCallOptions&, std::string_view body);
    void callAsync(const RpcEndpoint&, const RpcCallOptions&, std::string body,
                   RpcCallback callback);
};
```

v1 Server 至少提供：

```cpp
class RpcServer {
public:
    Status registerService(std::string service, std::string method,
                           RpcHandler handler);
    Status start(uint16_t port);
    void stop(std::chrono::seconds grace_period);
};
```

具体字段和编码以专题文档为准。

## 7. 生命周期

启动顺序：加载配置 -> 初始化日志和指标 -> 创建线程池 -> 启动网络 -> 初始化 Registry（v1.1）-> 构建 Tool Registry -> 启动 MCP 传输 -> 标记 ready。

停止顺序：停止接收新请求 -> 注销 Registry 并停止心跳 -> 等待进行中调用最多 30 秒 -> 关闭 SSE -> 关闭连接池和 TCP -> 停止线程池 -> 退出。

## 8. 统一约束

- 代码 C++17；命名、文件名、日志 key 使用英文；
- 线上整数使用 Big Endian，禁止发送 packed struct；
- metadata 上限 64 KiB，body 上限 16 MiB，单连接未完成请求上限 1024；
- v1 禁止压缩、JSON RPC body、流式 RPC 和自动重试非幂等方法；
- 所有新增功能必须同时补充单测、错误路径测试和对应阶段验收条目。
