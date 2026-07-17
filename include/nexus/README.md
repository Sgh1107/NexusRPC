# Public Headers

Public C++17 headers are grouped by ownership:

- `net/`: event loop, channels, TCP server and connection interfaces;
- `rpc/`: status, endpoint, protocol, client and server interfaces;
- `mcp/`: JSON-RPC, tools, sessions and transports;
- `observability/`: logging, metrics and health interfaces.

Implementation details belong in `src/` and must not leak into this tree.
