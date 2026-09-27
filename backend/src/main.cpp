#include <cstdlib>

#include "http_server.h"

// merging_server：机场拼车匹配工具后端入口
// 用法: merging_server [端口]，默认 8080
int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    return merging::run_server(port);
}
