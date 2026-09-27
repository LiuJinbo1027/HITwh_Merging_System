#include "http_server.h"

#include <chrono>

#include "httplib.h"
#include "nlohmann/json.hpp"

namespace merging {

namespace {

using nlohmann::json;

// 统一响应信封：成功 {"code":0,"message":"ok","data":...}；失败 code 非 0
void reply_ok(httplib::Response& res, const json& data) {
    res.set_content(json{{"code", 0}, {"message", "ok"}, {"data", data}}.dump(),
                    "application/json");
}

void reply_error(httplib::Response& res, int code, const std::string& message) {
    res.status = 400;
    res.set_content(json{{"code", code}, {"message", message}, {"data", nullptr}}.dump(),
                    "application/json");
}

}  // namespace

int run_server(int port) {
    const auto start = std::chrono::steady_clock::now();

    httplib::Server svr;
    svr.set_logger(nullptr);  // 演示程序，不刷请求日志

    // FR 联调：健康检查，返回服务运行时长
    svr.Get("/api/health", [&](const httplib::Request& /*req*/, httplib::Response& res) {
        auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
        reply_ok(res, json{{"uptime_ms", uptime_ms}});
    });

    if (!svr.bind_to_port("127.0.0.1", port)) {
        fprintf(stderr, "错误: 无法绑定 127.0.0.1:%d（端口被占用？）\n", port);
        return 1;
    }
    printf("merging_server 已启动: http://127.0.0.1:%d/api/health\n", port);
    return svr.listen_after_bind() ? 0 : 1;
}

}  // namespace merging
