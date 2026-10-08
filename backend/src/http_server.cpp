#include "http_server.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>

#include "httplib.h"
#include "nlohmann/json.hpp"
#include "service.h"

namespace merging {

namespace {

using nlohmann::json;

// 统一响应信封：成功 {"code":0,"message":"ok","data":...}；失败 code 非 0（HTTP 400）
void reply_ok(httplib::Response& res, const json& data) {
    res.set_content(json{{"code", 0}, {"message", "ok"}, {"data", data}}.dump(),
                    "application/json");
}

void reply_error(httplib::Response& res, int code, const std::string& message) {
    res.status = 400;
    res.set_content(json{{"code", code}, {"message", message}, {"data", nullptr}}.dump(),
                    "application/json");
}

// 路由统一入口：解析 JSON body → Service 调用 → 信封；异常兜底 50001
void dispatch(const httplib::Request& req, httplib::Response& res,
              const std::function<Result(const json&)>& handler) {
    try {
        json body = json::object();
        if (!req.body.empty()) body = json::parse(req.body);
        const Result result = handler(body);
        if (result.code == 0) {
            reply_ok(res, result.data);
        } else {
            reply_error(res, result.code, result.message);
        }
    } catch (const std::exception& e) {
        reply_error(res, 50001, std::string("内部错误: ") + e.what());
    }
}

// 路径参数 {id}；非法数字返回 -1（随后由 Service 报 40401 资源不存在）
int path_id(const httplib::Request& req) {
    try {
        return std::stoi(req.matches[1].str());
    } catch (const std::exception&) {
        return -1;
    }
}

bool parse_i64(const std::string& text, int64_t* out) {
    if (text.empty()) return false;
    try {
        size_t pos = 0;
        const long long value = std::stoll(text, &pos);
        if (pos != text.size()) return false;
        *out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

int64_t system_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int run_server(int port) {
    const auto start = std::chrono::steady_clock::now();

    Service service;
    httplib::Server svr;
    svr.set_logger(nullptr);  // 演示程序，不刷请求日志

    // FR 联调：健康检查，返回服务运行时长
    svr.Get("/api/health", [&](const httplib::Request& /*req*/, httplib::Response& res) {
        const auto uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - start)
                                   .count();
        reply_ok(res, json{{"uptime_ms", uptime_ms}});
    });

    // —— 乘客（FR-1~3、FR-6）——

    svr.Post("/api/passengers", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json& body) { return service.create_passenger(body); });
    });

    svr.Put(R"(/api/passengers/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        const int id = path_id(req);
        dispatch(req, res, [&](const json& body) { return service.update_passenger(id, body); });
    });

    svr.Delete(R"(/api/passengers/(\d+))",
               [&](const httplib::Request& req, httplib::Response& res) {
                   const int id = path_id(req);
                   dispatch(req, res, [&](const json&) { return service.cancel_passenger(id); });
               });

    svr.Post(R"(/api/passengers/(\d+)/agree)",
             [&](const httplib::Request& req, httplib::Response& res) {
                 const int id = path_id(req);
                 dispatch(req, res, [&](const json&) { return service.agree(id); });
             });

    svr.Post(R"(/api/passengers/(\d+)/reject)",
             [&](const httplib::Request& req, httplib::Response& res) {
                 const int id = path_id(req);
                 dispatch(req, res, [&](const json&) { return service.reject(id); });
             });

    // —— 匹配（FR-4/17/10/11）——

    svr.Post("/api/match/trigger", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.trigger_match(); });
    });

    svr.Post("/api/match/optimize", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.optimize(); });
    });

    svr.Get("/api/match/pool", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.pool(); });
    });

    svr.Get("/api/match/groups", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.groups(); });
    });

    // —— 事件 / 统计 / 成团完成（FR-12/13/14）——

    svr.Get("/api/events", [&](const httplib::Request& req, httplib::Response& res) {
        int64_t since_id = 0;
        int64_t limit = 100;
        const std::string since_text = req.get_param_value("since_id");
        if (!since_text.empty() && !parse_i64(since_text, &since_id)) {
            reply_error(res, 40001, "since_id 必须为整数");
            return;
        }
        const std::string limit_text = req.get_param_value("limit");
        if (!limit_text.empty() && !parse_i64(limit_text, &limit)) {
            reply_error(res, 40001, "limit 必须为整数");
            return;
        }
        since_id = std::max<int64_t>(since_id, 0);
        limit = std::clamp<int64_t>(limit, 1, 1000);
        dispatch(req, res, [&](const json&) { return service.events(since_id, limit); });
    });

    svr.Get("/api/stats", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.stats(); });
    });

    svr.Post(R"(/api/groups/(\d+)/complete)",
             [&](const httplib::Request& req, httplib::Response& res) {
                 const int id = path_id(req);
                 dispatch(req, res, [&](const json&) { return service.complete_group(id); });
             });

    // —— 虚拟乘客（FR-8/9）——

    svr.Post("/api/virtual/generate", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json& body) { return service.virtual_generate(body); });
    });

    svr.Post("/api/virtual/stream/start", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json& body) { return service.stream_start(body); });
    });

    svr.Post("/api/virtual/stream/stop", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.stream_stop(); });
    });

    // —— 配置与重置（FR-15/16）——

    svr.Get("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.get_config(); });
    });

    svr.Put("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json& body) { return service.set_config(body); });
    });

    svr.Post("/api/reset", [&](const httplib::Request& req, httplib::Response& res) {
        dispatch(req, res, [&](const json&) { return service.reset(); });
    });

    if (!svr.bind_to_port("127.0.0.1", port)) {
        fprintf(stderr, "错误: 无法绑定 127.0.0.1:%d（端口被占用？）\n", port);
        return 1;
    }
    printf("merging_server 已启动: http://127.0.0.1:%d/api/health (启动于 %lld)\n", port,
           static_cast<long long>(system_now_ms()));
    return svr.listen_after_bind() ? 0 : 1;
}

}  // namespace merging
