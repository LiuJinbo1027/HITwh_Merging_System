#pragma once

#include <functional>
#include <vector>

#include "model.h"
#include "pool.h"

namespace merging {

// 引擎可配参数（P2 由 /api/config 注入；默认值 = contract 第 5 节）
struct EngineConfig {
    int vehicle_capacity = 4;  // 单车载客上限
    int min_group_size = 2;    // 成团总人数下限（party_size 之和）
    int64_t proposal_ttl_ms = 60000;
};

// 贪心匹配引擎：run_once() 一轮扫描形成提案（★核心，实现见 manual_A P1 指引）
class MatchEngine {
   public:
    explicit MatchEngine(MatchPool* pool, EngineConfig cfg = {});

    // 一轮贪心匹配。种子优先级：party_size 降序 → start_min 升序 → id 升序；
    // 组不成 ≥min_group_size 人团的种子记入"本轮已尝试"，换下一种子，防止死循环。
    // now_ms 由调用方注入假时钟（测试禁止真实时间）。返回本轮形成的提案。
    std::vector<Proposal> run_once(int64_t now_ms);

    // 事件出口：默认空实现；P2 接 EventLog。P1 只发 proposed 事件。
    void set_event_sink(std::function<void(const Event&)> sink) { sink_ = std::move(sink); }

    // 运行时更新参数（P2 /api/config 的 proposal_ttl_ms 即改即生效）
    void set_config(const EngineConfig& cfg) { cfg_ = cfg; }

    // reset 用：清空提案/事件 id 计数（P2 /api/reset）
    void reset() {
        next_proposal_id_ = 1;
        next_event_id_ = 1;
    }

    int proposal_count() const { return next_proposal_id_ - 1; }

   private:
    // 性别偏好校验（契约语义：偏好只约束「同车其他成员」，不含本人——contract §1）。
    // 入团不变量：现有成员间偏好两两已满足（每次加入都做过双向校验），
    // 因此只校验候选 x 的两个方向，无需团内两两重验：
    //   (1) x 的偏好约束现有成员；(2) 现有成员的偏好约束 x。
    bool gender_compatible(const std::vector<int>& member_ids, int candidate_id) const;

    void emit_proposed(const Proposal& proposal, int64_t now_ms);

    MatchPool* pool_;
    EngineConfig cfg_;
    std::function<void(const Event&)> sink_;
    int next_proposal_id_ = 1;
    int64_t next_event_id_ = 1;
};

}  // namespace merging
