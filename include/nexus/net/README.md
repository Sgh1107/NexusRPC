# Network Headers

Public `libnexus_net` interfaces currently include:

- `buffer.h`: dynamically growing byte buffer for non-blocking IO;
- `channel.h`: file-descriptor event callbacks and interest-mask management;
- `event_loop.h`: Linux epoll loop, eventfd wakeup, and cross-thread task dispatch.

The network runtime is Linux-only in v1 because it depends on epoll and eventfd. Non-Linux CMake configurations retain the target boundary but do not compile the Linux implementation sources.
