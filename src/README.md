# Source Layout

Implementation ownership is split into:

- `net/`: epoll and TCP primitives;
- `rpc/`: codec, client, server and governance;
- `mcp/`: JSON-RPC, transports, sessions and tools;
- `registry/`: registry abstraction and Redis adapter;
- `observability/`: logs, metrics and health checks;
- `pool/`: thread and memory pools;
- `utils/`: configuration, status helpers and common utilities.

Phase 0 contains only this boundary documentation.
