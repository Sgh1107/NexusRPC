# NexusRPC 测试与验收规范

## 1. 测试分层

### 单元测试

覆盖 `Buffer`、EventLoop 投递、协议头编解码、FrameParser、metadata、Status、Tool 映射、JSON-RPC 校验、Protobuf schema 转换、Session 状态、LoadBalancer、CircuitBreaker、RateLimiter 和 Config。

### 集成测试

v1.0 测试 Weather Service -> RPC Client -> RPC Server 的真实 TCP 闭环，以及 stdio Gateway 的 `initialize`、`tools/list`、`tools/call`。v1.1 使用 Docker Compose 启动 Redis、多个 Weather、Echo 和 Gateway，测试注册、发现、下线、负载均衡和 MCP HTTP。

### 诊断测试

v1 CI 必须包含普通编译、单元测试、ASan、cppcheck。v1.1 增加 TSan、UBSan、clang-tidy 和集成测试。macOS 只运行纯工具模块测试。

## 2. 核心测试用例

| 模块 | 必测行为 |
|---|---|
| Buffer | 半包、粘包、扩容、读写边界 |
| Protocol | Big Endian、magic/version、长度溢出、最大消息、未知类型 |
| Network | accept 分发、ET 循环读写、跨线程唤醒、优雅关闭 |
| RPC | 注册/查找 handler、成功响应、解码失败、超时、断连 |
| Tool | 名称规范、Schema、必填和基础类型、结果双格式 |
| MCP | parse error、notification 无响应、Session、未知方法、工具错误 |
| Registry | Lua 原子性、TTL、懒清理、Pub/Sub 重连全量同步、缓存降级 |
| Governance | RoundRobin、ConsistentHash、熔断三态、令牌桶、幂等重试 |

## 3. v1.0 验收

1. 目录和 CMake target 按 `docs/design.md` 建立；
2. Weather Service 使用 RPC Server SDK 监听 `9601`；
3. stdio Gateway 启动后完成 MCP initialize；
4. Inspector 可列出 `weather.get_current`；
5. Inspector 调用工具得到 text JSON 和 structuredContent；
6. RPC 单元、MCP 单元、TCP 端到端测试通过；
7. ASan CI 通过；
8. Echo Service 监听 `9602`，可用于 RPC benchmark。

## 4. v1.1 验收

1. 多个实例可注册并被发现；
2. 主动上下线工具表感知目标小于 1 秒；
3. 异常退出通过 TTL 最终移除，允许不超过 15 秒；
4. Redis 断线重连后完成全量恢复；
5. 连接池按实例工作，实例下线不接新请求；
6. Streamable HTTP 的 POST、GET SSE、DELETE 和 Session 通过 Inspector/Claude Desktop 验证；
7. 健康检查和 Prometheus 指标可访问；
8. Docker Compose 集成测试通过。

## 5. 性能报告

Echo benchmark 测量 Protobuf 编解码后的 RPC Echo，不测纯 TCP Echo。报告必须记录客户端数量、连接数、并发、payload、预热时间、持续时间、日志等级、CPU 和是否绑核。MVP 不以 50 万 QPS作为阻塞门槛；v1.1 发布必须提供结果和瓶颈分析。

P99 测试至少覆盖本机 loopback 1KB payload 和跨节点 1KB payload。长稳测试当前只提供脚本、监控项和判定方法，正式 release 前执行 7x24 小时测试。

## 6. 覆盖率

核心模块 line coverage 目标为 60%，范围是 `net`、`rpc`、`mcp`、`registry`、`codec`。examples、benchmarks、scripts 不纳入硬门槛。Branch coverage 只作为报告指标。
