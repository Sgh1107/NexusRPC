# NexusRPC RPC 二进制协议

## 1. 范围

本文档定义 v1 Unary RPC 的线上帧格式。协议只承载 Protobuf body，JSON 仅属于 MCP 层或日志层。所有多字节整数使用网络字节序（Big Endian）。不得直接发送 C++ packed struct。

## 2. 帧布局

```text
+----------------------+ 0
| Fixed Header (32B)   |
+----------------------+ 32
| Metadata (variable)  |
+----------------------+
| Body (variable)      |
+----------------------+
```

`totalLength = 32 + metaLength + bodyLength`，包含固定头、metadata、body和自身字段。

## 3. 固定头字段

| 偏移 | 长度 | 字段 | 规则 |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `NRPC`，值 `0x4E525043` |
| 4 | 1 | version | v1 为 `1` |
| 5 | 1 | msgType | Request=0，Response=1，StreamData=2，StreamEnd=3，Ping=4，Pong=5；v1 只实现 0/1 |
| 6 | 1 | compressType | v1 必须为 0，其他值返回 UnsupportedCompression |
| 7 | 1 | serializeType | v1 必须为 0，表示 Protobuf |
| 8 | 4 | totalLength | Big Endian，最大不超过 32 + 64KiB + 16MiB |
| 12 | 8 | requestId | 连接内单调递增，随机初始值 |
| 20 | 4 | metaLength | metadata 字节数，最大 64KiB |
| 24 | 4 | bodyLength | body 字节数，最大 16MiB |
| 28 | 4 | reserved | v1 必须为 0，未来扩展 |

实现必须提供 `encodeHeader()` 和 `decodeHeader()`，并覆盖边界值、截断、错误 magic、错误版本和溢出检查。

## 4. Metadata

metadata 使用 Protobuf `map<string,string>` 的稳定编码消息。至少允许：`trace_id`、`deadline_unix_ms`、`client_id`、`tenant`。未知 key 必须保留并透传；单个 key/value 长度和总长度必须受限。

## 5. Body

Body 为业务生成的 Protobuf message。RPC Server 根据注册的 service/method 选择请求类型，handler 返回对应响应类型。协议层不做运行时 `.proto` 加载，也不做通用 JSON body。

## 6. 解码状态机

`NeedHeader -> NeedPayload -> Validate -> Dispatch -> WriteResponse`。Buffer 不足时保留数据并等待下一次读事件；长度非法、超限或协议字段不支持时返回协议错误，无法安全分帧时关闭连接。

## 7. 错误处理

- 无实例、连接失败、熔断：`UNAVAILABLE`；
- body 解码失败或基础参数错误：`INVALID_ARGUMENT`；
- 超时：`DEADLINE_EXCEEDED`；
- 非零压缩类型：`UNSUPPORTED_COMPRESSION`；
- 未注册 service/method：`NOT_FOUND`；
- handler 未分类异常：`INTERNAL`。

v1 不发送 Cancel 控制帧。StreamData/StreamEnd/Cancel 的枚举或字段只能作为保留值，收到未实现类型必须返回明确错误，不得静默处理。

## 8. 实现顺序

1. 定义协议常量和 `RpcHeader` 逻辑结构；
2. 实现显式 Big Endian 编解码；
3. 实现 metadata 编解码；
4. 实现完整帧解析器；
5. 接入 TcpConnection；
6. 增加 Server/Client Unary 闭环测试；
7. 增加异常连接和消息上限测试。
