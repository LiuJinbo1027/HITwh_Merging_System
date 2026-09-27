#pragma once

namespace merging {

// 启动 HTTP 服务并阻塞，直到进程收到终止信号。
// port: 监听端口（默认 8080）。返回进程退出码。
int run_server(int port);

}  // namespace merging
