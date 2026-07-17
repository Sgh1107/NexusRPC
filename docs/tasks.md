# NexusRPC 可执行任务清单

> 任务按依赖顺序排列。每项完成后必须补测试并更新对应文档。

## Phase 0：工程目录骨架

### TASK-001 建立项目目录 `[完成]`

- 目标：创建 `include/nexus`、`src`、`proto`、`examples`、`tests`、`benchmarks`、`scripts`、`tools`、`config`、`cmake` 和 `.github/workflows`。
- 输入：大纲目录结构、`docs/development_decisions.md`。
- 输出：目录、模块 README、文件职责清单、CMake 子目录入口占位。
- 依赖：无。
- 验收：目录结构与 `docs/design.md` 一致；空目录使用 `.gitkeep` 或 README 保留；不包含依赖安装和业务实现。
- 测试：已完成目录和关键文件静态检查。
- 风险：过早创建过多空文件导致职责漂移。

### TASK-002 建立构建目标占位 `[完成]`

- 目标：定义 `nexus_net`、`nexus_rpc`、`nexus_mcp`、`nexus_registry`、`nexus_observability`、examples 和 tests 的 target 边界。
- 输入：目录骨架。
- 输出：顶层和子目录 CMake 文件、target 依赖图。
- 依赖：TASK-001。
- 验收：构建文件只表达 target 和源文件边界，不要求安装依赖或完成编译验证。
- 测试：已完成 CMake 文件和 target 依赖方向静态检查；未执行环境验证。
- 风险：模块循环依赖。公共类型应放入低层公共头文件。

## Phase 1：MVP 网络与 RPC

### TASK-010 实现 Buffer 和 EventLoop

- 目标：实现 ET 读写 Buffer、eventfd 唤醒和任务投递。
- 输入：Linux epoll API。
- 输出：`include/nexus/net` 和 `src/net` 实现。
- 依赖：TASK-002。
- 验收：半包/粘包、跨线程投递和关闭流程正确。
- 测试：Buffer 单测、EventLoop 线程测试、ASan。
- 风险：Channel 生命周期和 EventLoop 所属线程不一致。

### TASK-011 实现 TCP Server/Connection

- 目标：主从 Reactor、accept 分发和非阻塞读写。
- 依赖：TASK-010。
- 输出：TCP Server API、Connection 回调和 Echo 骨架。
- 验收：Echo 可处理多次读写、断连和优雅停止。
- 测试：TCP 端到端、半包、并发连接。
- 风险：ET 模式未读尽数据造成连接饥饿。

### TASK-012 实现 RPC Frame Codec

- 目标：完成 32 字节头、metadata、Protobuf body 的编解码。
- 依赖：TASK-002。
- 验收：Big Endian、长度上限、非法字段和保留字段规则符合 `docs/protocol.md`。
- 测试：协议边界、截断、溢出、未知类型。
- 风险：直接发送 packed struct 或整数溢出。

### TASK-013 实现 RPC Server SDK

- 目标：实现 handler 注册、请求分发和 Unary response。
- 依赖：TASK-011、TASK-012。
- 验收：`registerService(service, method, handler)` 可注册并调用。
- 测试：成功、未找到、handler 错误、超时和异常映射。
- 风险：业务 handler 阻塞 IO 线程。

### TASK-014 实现 RPC Client SDK

- 目标：实现同步 call、future/callback 包装、deadline。
- 依赖：TASK-011、TASK-012。
- 验收：可调用 Weather 和 Echo；迟到响应不会污染后续请求。
- 测试：成功、连接失败、超时、断连、重复 request ID。
- 风险：v1 单连接串行，必须明确并发调用限制。

### TASK-015 建立示例 Proto 和服务

- 目标：创建 `rpc.proto`、`options.proto`、`weather.proto`、Weather 和 Echo 服务。
- 依赖：TASK-013、TASK-014。
- 验收：Weather 监听 `9601`，Echo 监听 `9602`，均通过 SDK 提供服务。
- 测试：真实 TCP 闭环。
- 风险：生成代码和手写 Stub 的职责混淆。

## Phase 2：MCP stdio MVP

### TASK-020 实现 JSON-RPC

- 目标：解析请求、构造响应和错误，严格处理 Notification。
- 依赖：TASK-002。
- 验收：支持 `initialize`、`initialized`、`ping`、错误码。
- 测试：解析失败、批量数组、错误 id、无响应 Notification。
- 风险：stdout 混入日志破坏 stdio 协议。

### TASK-021 实现 Protobuf Tool Registry

- 目标：从生成 Descriptor 和 options 生成 Tool 元数据。
- 依赖：TASK-015、TASK-020。
- 验收：生成 `weather.get_current`，Schema 使用 json_name，必填来源正确。
- 测试：基础类型、嵌套、map、repeated、Timestamp、wrapper。
- 风险：oneof 和 Any 按决策降级，不可伪装成完整支持。

### TASK-022 实现 stdio Gateway

- 目标：实现 tools/list、tools/call 和 RPC 路由。
- 依赖：TASK-014、TASK-020、TASK-021。
- 验收：MCP Inspector 完成 Weather 调用，返回 text 和 structuredContent。
- 测试：端到端和 stderr 日志隔离。
- 风险：MCP request ID 与 RPC request ID 映射错误。

## Phase 3：v1.1 Registry 与治理

### TASK-030 Redis Registry

- 目标：实现 Lua 注册、续约、注销、discover、清理和全量同步。
- 依赖：TASK-015、TASK-002。
- 验收：符合 `docs/registry.md`，Redis 失联可按缓存规则降级。
- 测试：多实例并发、TTL、Pub/Sub 重连、Redis 故障注入。
- 风险：增量事件丢失后未全量恢复。

### TASK-031 Tool Registry 动态更新

- 目标：将 Registry ServiceMeta 更新转换为本地工具表变化。
- 依赖：TASK-021、TASK-030。
- 验收：主动上下线小于 1 秒通知 SSE Session。
- 测试：重复事件、旧事件、实例过期。
- 风险：工具重名，需要明确冲突拒绝策略并记录日志。

### TASK-032 连接池和负载均衡

- 目标：按实例建池，加入 RoundRobin 和 ConsistentHash。
- 依赖：TASK-014、TASK-030。
- 验收：实例下线停止新分配，已有调用完成。
- 测试：并发 acquire/release、失败退避、空闲回收。
- 风险：连接池锁竞争和连接生命周期泄漏。

### TASK-033 熔断、限流、重试

- 目标：实现连续失败熔断、令牌桶、幂等重试。
- 依赖：TASK-032。
- 验收：HalfOpen 只有一个探测请求；非幂等方法不自动重试。
- 测试：状态转换、突发限流、退避和错误映射。
- 风险：将业务错误误判为可重试错误。

### TASK-034 Streamable HTTP

- 目标：实现 HTTP/1.1 POST JSON、GET SSE、DELETE 和 Session。
- 依赖：TASK-020、TASK-022、TASK-031。
- 验收：Inspector 和 Claude Desktop 均可 initialize、list、call。
- 测试：header 大小写、keep-alive、Session 过期、SSE 通知。
- 风险：自研 HTTP 解析器的请求边界和长连接清理。

### TASK-035 运维接口

- 目标：实现 Prometheus 指标、live/ready/detail 健康检查和优雅停止。
- 依赖：TASK-030、TASK-034。
- 验收：关闭顺序和 30 秒等待上限符合设计。
- 测试：依赖异常、停止期间新请求和指标计数。
- 风险：ready 与 live 状态混淆。

## Phase 4：v1.2 性能与可靠性

### TASK-040 Benchmark 与长稳脚本

- 目标：提供 RPC Echo benchmark、P99 报告模板和 7x24 验收脚本。
- 依赖：TASK-014、TASK-015。
- 验收：报告记录固定测试条件，脚本可启动、停止和收集数据。
- 测试：小规模 smoke benchmark。
- 风险：把 MCP 测试结果与 RPC Echo 指标混为一谈。

### TASK-041 可选增强

- 范围：Snappy/Zstd、完整 JSON Schema、协作式 Cancel、真实 progress、滑动窗口熔断、多路复用。
- 依赖：对应 v1.1 功能稳定并有基准数据。
- 验收：每项增强必须增加协议版本/兼容性说明和回归测试。
- 风险：在没有基准数据前进行无目标优化。
