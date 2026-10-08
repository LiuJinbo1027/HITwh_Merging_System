#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "config.h"
#include "event_log.h"
#include "model.h"
#include "pool.h"

namespace merging {

// 状态机操作结果：code==0 成功；非 0 为 contract §4 错误码，message 直接进响应信封
struct SmResult {
    int code = 0;
    std::string message = "ok";
};

// 进行中的提案记录：提案本体 + 已同意成员集合（全员同意即成团）
struct ProposalRecord {
    Proposal proposal;
    std::set<int> agreed;
};

// 状态机（manual_A P2 转移表，文档镜像 docs/state_machine.md）：
//   waiting →(引擎提案)→ proposed →(全员同意)→ grouped →(到点/手动)→ completed
//   任意非终态 →(取消)→ cancelled
//   waiting/proposed/grouped →(修改字段)→ 池中重入桶 / 解散提案 / 解散团
//   proposed →(拒绝/超时)→ 全体回 waiting 可重匹配
// 只在 Service 的单锁内调用（本类不加锁，不做参数校验）；now_ms 由调用方注入（假时钟可测）。
class StateMachine {
   public:
    StateMachine(MatchPool* pool, EventLog* events, const Config* config, std::mt19937* rng)
        : pool_(pool), events_(events), config_(config), rng_(rng) {}

    // 引擎一轮产出的提案登记（引擎已把成员置 proposed）
    void register_proposals(const std::vector<Proposal>& proposals);

    // FR-6 同意 / 拒绝；out_state 取值 "pending" / "grouped" / "dissolved"（进响应）
    SmResult agree(int passenger_id, int64_t now_ms, int* out_proposal_id, std::string* out_state);
    SmResult reject(int passenger_id, int64_t now_ms, int* out_proposal_id, std::string* out_state);

    // FR-3 取消 / FR-2 修改（新字段由 Service 校验合并后传入）
    SmResult cancel(int passenger_id, int64_t now_ms);
    SmResult update(int passenger_id, const Passenger& fields, int64_t now_ms);

    // FR-14 手动完成（到点自动完成复用同一逻辑）
    SmResult complete_group(int group_id, int64_t now_ms);

    // 假时钟驱动：提案超时 / 虚拟乘客自动同意或拒绝 / 团到点自动完成
    void tick(int64_t now_ms);

    const std::map<int, ProposalRecord>& proposals() const { return proposals_; }
    const std::map<int, Group>& groups() const { return groups_; }

    void reset();

   private:
    // 解散提案：全体成员回 waiting（事件由调用方按场景记录：rejected / proposal_expired）
    void dissolve_proposal(int proposal_id);
    // 解散团：全体成员回 waiting 并发 group_dissolved（调用方随后设置触发者终态）
    void dissolve_group(int group_id, int64_t now_ms);
    // 全员同意 → 成团：成员置 grouped、发 group_formed，返回新 group_id
    int form_group(const ProposalRecord& record, int64_t now_ms);

    MatchPool* pool_;
    EventLog* events_;
    const Config* config_;
    std::mt19937* rng_;
    std::map<int, ProposalRecord> proposals_;
    std::map<int, Group> groups_;
    int next_group_id_ = 1;
};

}  // namespace merging
