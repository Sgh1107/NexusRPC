# NexusRPC 开发决策与约束说明

## 1. 文档目标与交付范围

| 编号 | 问题 | 决策 |
|------|------|------|
| 1 | 详细开发文档面向谁？ | 同时满足个人从零实现、后续多人协作、AI 编程助手按步骤实现三种场景。文档应写成“可执行工程说明”，避免只写概念。每个阶段必须包含目标、输入输出、任务拆分、验收标准和测试要求。 |
| 2 | v1 最终交付物是否包含全部列出的内容？ | v1 分为 `v1.0 MVP` 与 `v1.1 完整版`。`v1.0 MVP` 必须交付：`libnexus_net`、RPC Server SDK、RPC Client SDK、MCP Gateway、Weather 示例、Echo 服务、MCP stdio 示例、最小单测和 CI。`v1.1` 补齐 Redis Registry、连接池、负载均衡、熔断、限流、Streamable HTTP、集成测试、压测脚本。完整长稳测试和 50w QPS 作为优化验收，不阻塞 MVP。 |
| 3 | 是否接受先做可运行 MVP？ | 接受，并推荐采用 MVP 优先。MVP 聚焦最短端到端链路：TCP + RPC 请求响应 + Protobuf + 单后端 + `tools/list` + `tools/call`。治理能力、Redis、性能优化分阶段加入。 |
| 4 | 1 周、2-3 周是否硬性工期？ | 不是硬性工期，只表达开发顺序和相对复杂度。每阶段以验收标准为准，不以日期为准。 |
| 5 | 必须优先支持的客户端？ | 优先级：1. MCP Inspector；2. Claude Desktop；3. 自定义 MCP Client；4. Cursor。MCP Inspector 更适合作为自动化和调试基线，Claude Desktop 作为真实用户场景验证。 |

---

## 2. 运行环境与工程约束

| 编号 | 问题 | 决策 |
|------|------|------|
| 6 | 目标 Linux 发行版 | Ubuntu 22.04 LTS 作为主开发和 CI 环境；Ubuntu 24.04 作为兼容验证；其他发行版不做 v1 保证。 |
| 7 | 编译器版本 | GCC 11+ 必须支持；Clang 14+ 尽力支持并在 CI 中编译验证。v1 不要求每个 sanitizer 都同时跑 GCC 和 Clang。 |
| 8 | C++ 标准 | 项目代码严格使用 C++17。构建工具或第三方库内部可以使用 C++20，但对外接口和本项目源码保持 C++17。 |
| 9 | 依赖管理 | 推荐“系统包优先，缺失依赖使用 FetchContent”。Ubuntu CI 通过 apt 安装 Protobuf、hiredis、openssl 等稳定依赖；nlohmann/json、googletest、benchmark 可用 FetchContent。暂不采用 vcpkg 作为默认方案。 |
| 10 | 是否允许成熟网络或 HTTP 库 | TCP/RPC 网络库自研。HTTP/1.1、SSE、Streamable HTTP 在 v1 可先实现最小自研版本，不引入 Boost.Asio/libevent/libuv。若后续发现 MCP HTTP 兼容成本过高，可只替换 HTTP 层，RPC 传输仍保持自研。 |
| 11 | Redis 客户端 | v1 使用 hiredis 同步接口，放入独立后台线程或任务队列中，避免阻塞 IO 线程。hiredis 异步接口作为 v1.1+ 优化项。 |
| 12 | macOS 支持 | v1 不要求 macOS 运行网络层，因为核心依赖 epoll。macOS 可编译和测试纯工具模块：JSON-RPC、Protobuf schema 转换、配置、错误码、部分单元测试。`nexus_net`、RPC Server、Gateway 网络服务在 macOS 跳过。 |

---

## 3. 进程和端口边界

| 编号 | 问题 | 决策 |
|------|------|------|
| 13 | `9600` 端口用途 | v1 中 `9600` 是 Backend Service 暴露 RPC Server 的默认端口，不是 Gateway 与 Core 进程间端口。Gateway/Core 同进程，不通过 `9600` 互调。示例中 Weather Service 默认监听 `9601`，Echo Service 默认监听 `9602`，避免与 Node 配置混淆。 |
| 14 | Backend 是否必须使用 NexusRPC RPC Server | 必须。示例 Backend 需要通过 NexusRPC RPC Server 对外服务，这样才能验证 SDK、协议、注册发现和 MCP 工具映射。普通 HTTP/TCP 服务不作为 v1 示例主线。 |
| 15 | Backend 服务分发方式 | v1 使用服务端注册函数：`registerService(serviceName, methodName, handler)`。不做 Protobuf 插件生成，不做运行时自动反射分发。Descriptor 只用于生成工具元数据和校验。 |
| 16 | 单进程 Node 角色 | 是。NexusRPC Node 同时承担 MCP Server、RPC Client、Registry Watcher、Tool Registry、健康检查/指标 HTTP Server。是否承担 RPC Server 由配置决定；默认 Gateway 示例不对外提供业务 RPC Server。 |

---

## 4. RPC 接口与代码生成

| 编号 | 问题 | 决策 |
|------|------|------|
| 17 | v1 是否手写 Stub | 是。v1 确定手写 Stub，不做 Protobuf 插件。详细文档需要提供统一 Stub 模板：构造 `RpcRequest`、设置 service/method、序列化 request、调用 `RpcClient::call`、反序列化 response。 |
| 18 | Protobuf 是否使用标准 `service/rpc` | 使用标准 `service` / `rpc` 声明作为接口契约。自定义 `ServiceDescriptor` / `MethodDescriptor` 消息只用于注册中心传输和运行时元数据快照，不替代 proto 的 `service` 定义。 |
| 19 | 方法元数据放在哪里 | 组合方案：proto 自定义 options 为主，运行时 `MethodDescriptor` 为派生结果，必要时允许配置文件覆盖少量运维项（timeout、rate limit）。业务语义如 description、is_tool、idempotent、required 应放在 proto options。 |
| 20 | 是否运行时加载 `.proto` | v1 不运行时加载 `.proto` 文件。编译阶段用 `protoc` 生成 C++ 文件，运行时使用生成代码携带的 Descriptor。 |
| 21 | RPC 是否只支持一元请求响应 | v1 只支持 Unary RPC。一元协议中可以保留 `StreamData`、`StreamEnd` 枚举值，但不实现。长任务进度 v1 不做真实后端进度流，仅预留接口。 |
| 22 | 其他 RPC 能力 | v1 支持：一元请求响应、异步 callback/future 包装、请求超时。v1.1 支持请求取消消息。单向通知、客户端流、服务端流、双向流、服务端主动推送均不属于 v1。 |

---

## 5. 协议头和传输协议

| 编号 | 问题 | 决策 |
|------|------|------|
| 23 | 整数字节序 | 协议线上编码统一使用网络字节序（Big Endian）。不能直接 `reinterpret_cast` packed struct 发送。实现必须显式 encode/decode 每个字段。 |
| 24 | `totalLength` 范围 | `totalLength = fixed_header_length + metadata_length + body_length`，包含固定头、metadata、body，也包含 `totalLength` 字段自身。v1 固定头长度为 32 字节。 |
| 25 | 最大消息大小 | v1 默认：metadata 最大 64KB，body 最大 16MB，单连接最大未完成请求数 1024。超限返回 `RESOURCE_EXHAUSTED` 或关闭异常连接；MCP 层映射为 `-32602` 或 `-32002`。 |
| 26 | 压缩是否实现 | v1 不实现压缩。`compressType` 字段保留，必须为 `none`。收到非 `none` 请求返回 `UNSUPPORTED_COMPRESSION`。Snappy/Zstd 进入 v1.2。 |
| 27 | `serializeType=json` | v1 不实现 JSON body，仅保留字段。线上请求必须为 Protobuf。debug 日志可以输出 JSON，但不改变协议。 |
| 28 | request ID 规则 | 使用单连接内单调递增 `uint64`，从随机初始值开始。要求单连接内不重复；不要求全局唯一。日志中的 traceId 承担跨连接关联。 |

---

## 6. 服务注册与 Redis 行为

| 编号 | 问题 | 决策 |
|------|------|------|
| 29 | 多实例并发安全 | v1 使用 Redis Lua 脚本保证注册、续约、下线时实例键和索引集合更新原子性。没有 Lua 的地方必须接受最终一致，但核心注册路径使用 Lua。 |
| 30 | Set 中过期实例清理 | 保留 Set 索引，但每次 discover 时校验实例键是否存在，并懒清理 Set 中的悬挂 instanceId。另加后台定时清理任务。暂不依赖 Keyspace Notifications。 |
| 31 | `<1s` 与 `≤15s` 指标 | 主动注册/主动下线通过 Pub/Sub 感知，目标 `<1s`。异常宕机依赖 TTL，允许 `≤15s`。文档中必须拆成两个指标。 |
| 32 | Pub/Sub 丢失恢复 | Gateway 断线重连后必须执行全量拉取，重建本地缓存。Pub/Sub 只作为增量通知，不作为唯一事实来源。 |
| 33 | Registry 失联规则 | 已缓存实例继续调用，最多 5 分钟；超过 5 分钟后拒绝新调用，返回 `UNAVAILABLE`。已进行中的调用继续。Redis 恢复后 Gateway 全量同步。Backend 在失联时无法注册/续约，但本地 RPC Server 继续接受已有直连请求。 |
| 34 | Redis 部署能力 | v1 只支持单机 Redis，可配置密码。TLS、Sentinel、Cluster 不属于 v1。 |

---

## 7. MCP 协议范围

| 编号 | 问题 | 决策 |
|------|------|------|
| 35 | Streamable HTTP 协议版本 | v1 只实现 HTTP/1.1，不考虑 HTTP/2。 |
| 36 | Streamable HTTP 响应策略 | v1 实现最小标准行为：普通 POST 返回 JSON；GET 建立 SSE 长连接用于服务端通知；长任务 POST 不返回 SSE 流，进度能力先预留。 |
| 37 | 批量 JSON-RPC | v1 不支持批量请求。收到数组形式请求返回 `InvalidRequest`。 |
| 38 | Notification | 严格支持 JSON-RPC Notification：无 `id` 的请求不返回响应，例如 `initialized`。 |
| 39 | MCP 会话 | HTTP 模式下 `initialize` 创建 Session，并在响应头返回 `Mcp-Session-Id`。未携带 Session 的非 initialize 请求默认拒绝。stdio 也创建内存 Session，但不需要 header。Session 空闲超时默认 30 分钟。服务端重启后 Session 全部失效，不持久化。 |
| 40 | HTTP 头行为 | 请求头大小写不敏感。v1 要求 `Content-Type: application/json`。`Accept` 支持 `application/json` 与 `text/event-stream`。启用 HTTP keep-alive。CORS 默认关闭，可配置允许所有来源用于本地调试。 |
| 41 | 认证方式 | v1 默认不实现认证。HTTP 可选固定 Header 共享密钥：`Authorization: Bearer <token>`。stdio 不认证。生产建议由反向代理负责。 |
| 42 | `resources/*` 和 `prompts/*` | v1 不注册 resources/prompts 能力。客户端直接调用时返回 `MethodNotFound`。代码中可预留接口但不对外声明能力。 |
| 43 | `tools/list` 分页 | v1 不分页，忽略 cursor 或返回不支持 cursor。工具数量预期较小。v1.1 可补分页。 |
| 44 | `tools/list_changed` 通知范围 | 仅通知已完成 `initialize` 且建立 GET/SSE 流的 Session。没有 SSE 流的 Session 通过下次 `tools/list` 拉取最新结果。 |

---

## 8. 工具映射和数据转换

| 编号 | 问题 | 决策 |
|------|------|------|
| 45 | 工具名规则 | 固定为 `service_name.method_name`。service 与 method 均使用 snake_case 小写，合法字符为 `[a-z0-9_]+`。从 Protobuf `WeatherService.GetCurrent` 默认转换为 `weather.get_current`，也允许通过 option 显式覆盖。 |
| 46 | Protobuf 类型支持范围 | v1 支持：整数、浮点、bool、string、bytes、enum、nested message、repeated、map、proto3 optional、Timestamp、Duration、wrapper types。v1 不支持 Any 的语义展开，按 object/string fallback。oneof 转为 `oneOf` 作为 v1.1。 |
| 47 | JSON 字段命名 | 输入同时接受 proto 字段名和 `json_name`；输出固定使用 Protobuf JSON Mapping 的 `json_name`。Schema 中默认使用 `json_name`。 |
| 48 | 必填字段来源 | 使用自定义 option 或 `google.api.field_behavior = REQUIRED`。proto3 普通字段默认可选。`proto3 optional` 只表示 presence，不自动等于 required。 |
| 49 | JSON Schema 校验 | v1 只做必填字段和基础类型校验，不引入完整 JSON Schema 库。复杂约束交给 Backend。v1.1 可引入第三方 JSON Schema validator。 |
| 50 | 工具调用结果格式 | v1 同时返回文本和结构化数据：`content[0].type = text`，text 为紧凑 JSON 字符串；同时在 MCP 支持时返回 `structuredContent`。不支持结构化的客户端仍可读 text。 |
| 51 | Backend 业务错误分类 | 需要区分：参数错误、业务拒绝、未找到、内部异常、可重试错误、不可重试错误。映射到统一 `StatusCode`，再映射 JSON-RPC 错误。 |

---

## 9. 取消、超时、重试与进度

| 编号 | 问题 | 决策 |
|------|------|------|
| 52 | Cancel 如何映射 RPC | 维护 MCP request id 到 RPC request id 的映射，不直接复用。RPC 使用独立 `uint64 requestId`，MCP id 可能是字符串。 |
| 53 | 是否真正中断 Backend | v1 只停止等待并丢弃迟到响应，不保证中断 Backend 业务执行。v1.1 增加 Cancel 消息后，Backend 可协作式取消。 |
| 54 | 是否发送独立 Cancel 消息 | v1 不发送。v1.1 增加 `msgType = Cancel` 或控制帧。 |
| 55 | 重试失败判定 | 只对连接建立失败、连接断开、`UNAVAILABLE`、可重试超时进行重试。不对 `INVALID_ARGUMENT`、业务拒绝、`INTERNAL` 默认重试。 |
| 56 | 重试是否只限幂等 | 是。只有显式标记 `idempotent=true` 的方法允许自动重试；未标记默认禁止重试。 |
| 57 | MCP 超时来源 | 取多层最小值：方法配置 timeout、全局 RPC deadline、HTTP server request timeout。v1 不从 MCP 请求参数读取自定义 timeout，除非后续定义私有扩展。 |
| 58 | progressToken 与进度 | v1 只解析和保存 `progressToken`，不实现真实后端进度。v1.1 通过独立进度回调或 Server Streaming 实现。若无流式 RPC，Backend 无法在返回前可靠向 Gateway 发送进度。 |

---

## 10. 限流、熔断和连接池

| 编号 | 问题 | 决策 |
|------|------|------|
| 59 | 熔断统计窗口 | v1 采用按服务实例 + 方法维度的连续失败次数。v1.1 改为滑动时间窗口。 |
| 60 | HalfOpen 探测 | HalfOpen 允许 1 个探测请求。成功则关闭熔断并清零失败计数；失败则重新 Open，并刷新熔断时间。 |
| 61 | 限流动态配置 | v1 读取启动配置，不热更新。v1.1 支持周期性 reload。 |
| 62 | 来源 ID | 优先级：认证身份 > HTTP Header `X-Client-Id` > MCP Session ID > RPC metadata `client_id` > remote address。 |
| 63 | 连接池维度 | 按服务实例 ID 建池，实例元数据包含 host:port。实例下线后停止分配新请求，已有连接等待空闲后关闭；进行中的请求允许完成。 |
| 64 | 连接池能力 | v1 支持空闲超时、连接失败退避、单连接串行请求。v1 不支持单连接多路复用、请求乱序返回、断线后未完成请求自动恢复。v1.1 可支持 pipelining 或多路复用。 |

---

## 11. 配置、日志、指标和运维

| 编号 | 问题 | 决策 |
|------|------|------|
| 65 | 配置格式 | 推荐 TOML。比 YAML 解析简单，比 JSON 可读性好，适合 C++ 项目。 |
| 66 | 配置覆盖 | 支持环境变量和命令行覆盖。优先级：命令行 > 环境变量 > 配置文件 > 默认值。 |
| 67 | 配置热更新 | v1 不做通用热更新。日志级别可通过管理接口临时调整。限流、熔断、Registry、端口等配置需重启生效。 |
| 68 | 结构化日志 | 默认文本日志，支持 JSON 日志配置。字段至少包括：time、level、logger、trace_id、request_id、service、method、session_id、error_code、latency_ms、message。 |
| 69 | 指标暴露 | Prometheus `/metrics` 为默认方式。JSON 管理接口可作为辅助。 |
| 70 | 健康检查 | 提供 `/health/live`、`/health/ready`、`/health/detail`。live 只表示进程活着；ready 检查端口、Registry、关键线程；detail 输出依赖状态。 |
| 71 | 优雅启停 | 默认最大等待 30s。顺序：停止接收新 MCP/RPC 请求；从 Registry 注销并停止心跳；等待进行中调用；广播/关闭 SSE；关闭连接池；停止线程池；退出。 |

---

## 12. 测试和验收

| 编号 | 问题 | 决策 |
|------|------|------|
| 72 | 性能指标是否发布门槛 | 对 MVP 不是发布门槛。对 v1.1 是优化目标。最终正式 release 应给出 benchmark 报告，但不因未达 50w QPS 阻塞功能闭环。 |
| 73 | 50w QPS 测量对象 | 指 Protobuf 编解码后的 RPC Echo，不是纯 TCP Echo，也不是 MCP tools/call。MCP tools/call 单独测延迟和吞吐。 |
| 74 | P99 测试固定项 | 需要固定并记录：客户端数量、连接数、并发数、payload 大小、预热时间、测试时长、日志级别、CPU 信息。v1 不强制绑核；性能报告中说明是否绑核。 |
| 75 | 7×24 测试 | 当前版本不必须跑满 7×24，但必须提供脚本和验收方法。正式 release 前再执行。 |
| 76 | 集成测试依赖 | 集成测试可以使用 Docker Compose 自动启动 Redis、Backend、Gateway。正式部署不支持容器编排，不影响测试使用容器。 |
| 77 | CI 内容 | v1 CI 必须运行：编译、单元测试、ASan、cppcheck。v1.1 增加 TSan、UBSan、clang-tidy、集成测试。benchmark 和 MCP Inspector 兼容性测试可手动或 nightly。 |
| 78 | 覆盖率口径 | 60% 指核心模块 line coverage：net、rpc、mcp、registry、codec。不要求 examples、benchmarks、scripts 纳入。branch coverage 作为观察指标，不设硬门槛。 |

---

## 13. 文档形式

| 编号 | 问题 | 决策 |
|------|------|------|
| 79 | 最终生成哪些文档 | 采用“一个主开发文档 + 若干专题文档”。建议：`docs/design.md`（总详设）、`docs/protocol.md`（RPC 二进制协议）、`docs/mcp.md`（MCP 行为）、`docs/registry.md`（Redis 注册中心）、`docs/testing.md`（测试验收）、`docs/tasks.md`（可拆解任务）。 |
| 80 | 详细开发文档包含什么 | 需要包含：模块职责、核心类接口、文件级任务、状态机、时序图、错误处理矩阵、配置项表、Redis Key 规范、协议二进制布局、测试用例清单、任务依赖关系、阶段完成标准。函数级实现规则只对复杂模块写，不要求每个函数展开。 |
| 81 | 文档和代码语言 | 文档使用中文。代码、类名、文件名、字段名、日志 key、注释统一英文。中文只出现在面向用户的 README 和设计解释中。 |
| 82 | 是否写成 GitHub Issues/TODO | 是。`docs/tasks.md` 应按 Issue 形式组织，每项包含：目标、输入输出、依赖、验收标准、测试要求、风险。这样适合个人执行、多人协作，也适合 AI 编程助手逐项实现。 |

---

## 14. 对 README 大纲的修正结论

以下问题需要在后续更新 README 或 `docs/design.md` 时按本决策修正：

- Redis Pub/Sub 不是可靠消息系统，必须增加断线重连后的全量同步。
- Redis 实例 TTL 到期不会自动从 Set 中删除 instanceId，必须使用 discover 懒清理 + 后台清理任务。
- 服务发现指标拆分为：主动变更 `<1s`，异常宕机 `≤15s`。
- Protobuf proto3 普通字段不能用 `is_required()` 判断必填，必须使用自定义 option 或 `google.api.field_behavior = REQUIRED`。
- v1 不实现压缩、JSON body、流式 RPC；协议字段保留但收到相关请求应返回明确错误。
- `notifications/progress`、请求取消和真实 RPC 中断不作为 MVP 功能，先预留接口，v1.1 再做协作式取消和进度上报。
- `9600` 不作为 Gateway/Core 同进程内部端口，而是 RPC Server 默认端口或示例服务端口配置基准。
- MCP 工具结果 v1 同时提供 text JSON 字符串和可选 structuredContent。
- AI 工具注册中心在 v1 中不是独立服务，而是 Gateway 内部 Tool Registry + Redis 中的服务元数据。未来如果需要可拆成独立 Tool Registry 服务。

---

## 15. 推荐的阶段化开发计划

### v1.0 MVP

目标：跑通最短端到端链路。

必须完成：
- CMake 工程、CI、基础目录结构。
- `libnexus_net`：EventLoop、Channel、TcpServer、TcpConnection、Buffer。
- RPC 协议：固定头 encode/decode、Protobuf body、Unary request/response。
- RPC Server SDK：手写注册 handler。
- RPC Client SDK：同步 call + future 包装。
- Weather Service：使用 NexusRPC Server 暴露 `weather.get_current`。
- MCP stdio Gateway：支持 `initialize`、`tools/list`、`tools/call`。
- 最小单元测试和端到端测试。

验收标准：
- 本地启动 Weather Service 与 MCP stdio Gateway。
- MCP Inspector 能看到 `weather.get_current` 并调用成功。
- 单元测试和 ASan CI 通过。

### v1.1 治理能力版

目标：补齐分布式服务治理和 HTTP MCP 接入。

必须完成：
- Redis Registry：注册、发现、心跳、下线、全量同步、Pub/Sub 增量通知。
- 连接池、RoundRobin、ConsistentHash。
- 熔断、限流、超时、幂等重试。
- Streamable HTTP：POST JSON、GET SSE、Session 管理。
- Prometheus `/metrics` 与健康检查。
- 集成测试、Docker Compose 测试环境。

验收标准：
- 多个 Weather 实例注册到 Redis，Gateway 自动发现并负载均衡。
- 主动上下线在 1 秒内触发工具列表刷新。
- Redis 短暂断线后 Gateway 可全量恢复。
- MCP Inspector 和 Claude Desktop 均可调用工具。

### v1.2 性能和可靠性版

目标：优化吞吐、延迟和长期稳定性。

候选内容：
- 压缩支持：Snappy/Zstd。
- 更完整 JSON Schema 校验。
- 协作式 Cancel 消息。
- 真实 progress 上报。
- 连接多路复用或 pipelining。
- 滑动窗口熔断。
- 7×24 长稳测试和 benchmark 报告。
