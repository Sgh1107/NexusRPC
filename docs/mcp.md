# NexusRPC MCP 行为规范

## 1. v1 范围

v1 只实现 MCP stdio 和最小 Streamable HTTP。MCP 方法包括 `initialize`、`initialized`、`ping`、`tools/list`、`tools/call`。不声明 resources、prompts、sampling、roots、elicitation 能力。批量 JSON-RPC 请求不支持。

v1.0 只交付 stdio；v1.1 交付 HTTP/1.1、POST JSON、GET SSE 和 Session。

## 2. JSON-RPC 处理

解析顺序：JSON 文本解析 -> 顶层对象校验 -> `jsonrpc == "2.0"` -> id 类型校验 -> method 校验 -> params 校验。数组直接返回 `-32600`。无 id 的 Notification 不返回响应。

错误码：`-32700` ParseError，`-32600` InvalidRequest，`-32601` MethodNotFound，`-32602` InvalidParams，`-32603` InternalError；工具错误使用 `-32001` 至 `-32005`。

## 3. Session

HTTP `initialize` 创建内存 Session，并在响应头写入 `Mcp-Session-Id`。除 initialize 外的请求必须携带有效 Session。Session 空闲 30 分钟过期，服务重启后全部失效。stdio 创建内部 Session，不使用 header。

Session 需要保护并发字段：`lastActiveAt`、初始化状态、客户端 capabilities、SSE 连接和 pending calls。Session 删除时必须关闭其 SSE 连接并清理 pending call。

## 4. 生命周期

`initialize` 成功后才允许业务方法；客户端发送 `initialized` Notification；`ping` 可在初始化后调用。重复 initialize 默认返回 InvalidRequest。服务端能力只声明实际实现的方法。

## 5. tools/list

Tool Registry 返回所有有效工具，不分页。工具名固定为 `service_name.method_name`，使用小写 snake_case。Registry 变更后，仅向已初始化且已建立 SSE 流的 Session 发送 `notifications/tools/list_changed`。

## 6. tools/call

调用步骤：查找工具 -> 校验基础类型与必填项 -> JSON 转请求 Protobuf -> RPC Client 调用 -> Protobuf JSON Mapping -> 构造结果。

成功结果同时提供：

- `content[0] = {"type":"text","text":"<compact JSON>"}`；
- `structuredContent = <JSON object>`（客户端支持时）。

MCP request id 和 RPC request id 独立保存映射。v1 只等待最终响应，不实现真实进度和取消。

## 7. Streamable HTTP

- `POST /mcp`：要求 `Content-Type: application/json`，普通请求返回 JSON；
- `GET /mcp`：要求有效 Session，建立 `text/event-stream` 长连接；
- `DELETE /mcp`：关闭 Session；
- 请求头大小写不敏感；
- 支持 HTTP keep-alive；
- CORS 默认关闭，可配置本地调试模式；
- 可选认证使用 `Authorization: Bearer <token>`。

GET SSE 只用于通知，不承担 v1 的长任务结果流。HTTP 请求超时、全局 RPC deadline、方法 timeout 取最小值。

## 8. 工具元数据

工具 description、is_tool、idempotent 和 required 主要来自 Protobuf options；timeout、rate limit 等运维项可由配置覆盖。v1 支持基础类型、bytes、enum、nested message、repeated、map、proto3 optional、Timestamp、Duration 和 wrapper types。oneof 完整 `oneOf` schema 延至 v1.1。

## 9. stdio 约束

stdin 按行读取 JSON-RPC，stdout 只输出协议响应，日志必须写 stderr 或文件。单条消息处理完成后刷新 stdout。输入 EOF 触发优雅停止。
