#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "config.h"
#include "engine.h"
#include "event_log.h"
#include "model.h"
#include "nlohmann/json.hpp"
#include "pool.h"
#include "state_machine.h"
#include "virtual_source.h"

namespace merging {

// 统一响应信封（contract §0）：code==0 成功（data 进 data 字段），否则 data=null
struct Result {
    int code = 0;
    std::string message = "ok";
    nlohmann::json data = nullptr;
};

// API 门面：全部读写的唯一入口，一把 std::mutex 串行（手册：httplib 多线程只转发请求，
// 不自己起线程）。自动行为（提案超时/虚拟流/自动匹配）由 tick 惰性驱动：每个公开方法进入时
// 先 tick 一次，轮询客户端（2s 事件轮询）即可保证持续推进；测试注入假时钟直接调 tick()。
class Service {
   public:
    Service();  // 真实时钟 + 固定默认随机种子（演示可复现）
    Service(std::function<int64_t()> clock, uint32_t seed);

    // 驱动超时/虚拟流/自动匹配（HTTP 层无需单独调用：各公开方法内部已 tick）
    void tick();

    // —— 乘客（FR-1~3、6）——
    Result create_passenger(const nlohmann::json& body);
    Result update_passenger(int passenger_id, const nlohmann::json& body);
    Result cancel_passenger(int passenger_id);
    Result agree(int passenger_id);
    Result reject(int passenger_id);

    // —— 匹配（FR-4/17/10/11）——
    Result trigger_match();
    Result optimize();
    Result pool();
    Result groups();

    // —— 事件/统计/成团完成（FR-12/13/14）——
    Result events(int64_t since_id, int64_t limit);
    Result stats();
    Result complete_group(int group_id);

    // —— 虚拟乘客（FR-8/9）——
    Result virtual_generate(const nlohmann::json& body);
    Result stream_start(const nlohmann::json& body);
    Result stream_stop();

    // —— 配置与重置（FR-15/16）——
    Result get_config();
    Result set_config(const nlohmann::json& body);
    Result reset();

   private:
    int64_t now_ms() const { return clock_(); }
    void tick_locked(int64_t now);
    std::vector<Proposal> run_match_locked(int64_t now);
    std::vector<int> generate_locked(int count, const std::string& date, int64_t now);
    nlohmann::json config_to_json() const;
    void apply_engine_config_locked();

    std::mutex mu_;
    std::function<int64_t()> clock_;
    uint32_t seed_ = 42;

    MatchPool pool_;
    EventLog events_;
    Config config_;
    std::mt19937 rng_;  // 虚拟乘客分布 + 自动同意概率共用（注入种子可复现）
    MatchEngine engine_;
    StateMachine sm_;
    VirtualGenerator generator_;

    int64_t next_auto_match_ms_ = 0;  // 下一次自动匹配的 epoch ms（auto_match 关闭时无意义）
    bool stream_running_ = false;
    int64_t next_stream_ms_ = 0;  // 下一批虚拟乘客生成时间（epoch ms）
};

}  // namespace merging
