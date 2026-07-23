# RPC Headers

Public RPC status, frame protocol, raw transport, and typed Protobuf client and
server APIs. The raw body interfaces are the transport boundary; application
code should use `RpcClient::call<Request, Response>` and
`RpcServer::registerService<Request, Response>` with generated Protobuf types.
