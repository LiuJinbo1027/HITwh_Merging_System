#include "state_machine.h"

namespace merging {

namespace {

// 提案/团解散的公共动作：成员全部回 waiting（清空 proposal_id / group_id）
void return_members_to_waiting(MatchPool* pool, const std::vector<int>& member_ids) {
    for (int id : member_ids) pool->set_status(id, Status::kWaiting);
}

}  // namespace

void StateMachine::register_proposals(const std::vector<Proposal>& proposals) {
    for (const auto& p : proposals) {
        ProposalRecord record;
        record.proposal = p;
        proposals_[p.id] = std::move(record);
    }
}

SmResult StateMachine::agree(int passenger_id, int64_t now_ms, int* out_proposal_id,
                             std::string* out_state) {
    Passenger* p = pool_->get(passenger_id);
    if (p == nullptr) return {40401, "乘客不存在"};
    if (p->status != Status::kProposed) return {40901, "状态冲突：当前无待同意的提案"};
    const int proposal_id = p->proposal_id;
    const auto it = proposals_.find(proposal_id);
    if (it == proposals_.end())
        return {40901, "状态冲突：提案已结束"};  // 防御：登记表与池状态不一致
    *out_proposal_id = proposal_id;

    ProposalRecord& record = it->second;
    record.agreed.insert(passenger_id);  // 重复同意幂等
    events_->append("agreed", {{"passenger_id", passenger_id}, {"proposal_id", proposal_id}},
                    now_ms);
    if (record.agreed.size() >= record.proposal.member_ids.size()) {
        form_group(record, now_ms);
        proposals_.erase(proposal_id);
        *out_state = "grouped";
    } else {
        *out_state = "pending";
    }
    return {};
}

SmResult StateMachine::reject(int passenger_id, int64_t now_ms, int* out_proposal_id,
                              std::string* out_state) {
    Passenger* p = pool_->get(passenger_id);
    if (p == nullptr) return {40401, "乘客不存在"};
    if (p->status != Status::kProposed) return {40901, "状态冲突：当前无待处理的提案"};
    const int proposal_id = p->proposal_id;
    *out_proposal_id = proposal_id;
    events_->append("rejected", {{"passenger_id", passenger_id}, {"proposal_id", proposal_id}},
                    now_ms);
    dissolve_proposal(proposal_id);  // 全体（含拒绝者）回 waiting，可再次被匹配
    *out_state = "dissolved";
    return {};
}

SmResult StateMachine::cancel(int passenger_id, int64_t now_ms) {
    Passenger* p = pool_->get(passenger_id);
    if (p == nullptr) return {40401, "乘客不存在"};
    const Status status = p->status;
    if (status == Status::kCancelled || status == Status::kCompleted) {
        return {40901, "状态冲突：乘客已结束"};
    }
    if (status == Status::kProposed) {
        dissolve_proposal(p->proposal_id);
    } else if (status == Status::kGrouped) {
        dissolve_group(p->group_id, now_ms);
    }
    pool_->set_status(passenger_id, Status::kCancelled);
    events_->append("cancelled", {{"passenger_id", passenger_id}}, now_ms);
    return {};
}

SmResult StateMachine::update(int passenger_id, const Passenger& fields, int64_t now_ms) {
    Passenger* p = pool_->get(passenger_id);
    if (p == nullptr) return {40401, "乘客不存在"};
    const Status status = p->status;
    if (status == Status::kCancelled || status == Status::kCompleted) {
        return {40901, "状态冲突：乘客已结束，不能修改"};
    }
    // 转移表：proposed/grouped 上的修改须先解散——本次修改可能破坏团内容量与性别偏好不变量
    // （不止改时间；改人数/性别/偏好同理），其余成员回池，本人按新字段重入桶
    if (status == Status::kProposed) {
        dissolve_proposal(p->proposal_id);
    } else if (status == Status::kGrouped) {
        dissolve_group(p->group_id, now_ms);
    }
    pool_->update(passenger_id, fields);
    events_->append("updated", {{"passenger_id", passenger_id}}, now_ms);
    return {};
}

SmResult StateMachine::complete_group(int group_id, int64_t now_ms) {
    const auto it = groups_.find(group_id);
    if (it == groups_.end()) return {40401, "团不存在或已结束"};
    const std::vector<int> member_ids = it->second.member_ids;  // 拷贝：下面要 erase
    groups_.erase(it);
    for (int id : member_ids) pool_->set_status(id, Status::kCompleted);
    events_->append("completed", {{"group_id", group_id}}, now_ms);
    return {};
}

void StateMachine::tick(int64_t now_ms) {
    // 1) 提案超时：deadline 到期 → proposal_expired，全体回池
    std::vector<int> expired;
    for (const auto& [id, record] : proposals_) {
        if (now_ms >= record.proposal.deadline_ms) expired.push_back(id);
    }
    for (int id : expired) {
        const auto it = proposals_.find(id);
        if (it == proposals_.end()) continue;
        events_->append("proposal_expired",
                        {{"proposal_id", id}, {"member_ids", it->second.proposal.member_ids}},
                        now_ms);
        dissolve_proposal(id);
    }

    // 2) 虚拟乘客自动同意：在提案内停留 ≥ virtual_agree_delay_ms 后按概率 agree，否则 reject
    std::vector<int> proposal_ids;
    proposal_ids.reserve(proposals_.size());
    for (const auto& [id, record] : proposals_) proposal_ids.push_back(id);
    for (int id : proposal_ids) {
        const auto rec_it = proposals_.find(id);
        if (rec_it == proposals_.end()) continue;
        const std::vector<int> member_ids = rec_it->second.proposal.member_ids;  // 处理中会解散
        for (int member_id : member_ids) {
            const auto prop_it = proposals_.find(id);
            if (prop_it == proposals_.end()) break;  // 提案已被本次处理解散
            const Passenger* p = pool_->get(member_id);
            if (p == nullptr || p->status != Status::kProposed || p->proposal_id != id) continue;
            if (!p->is_virtual) continue;
            if (now_ms - prop_it->second.proposal.created_at_ms < config_->virtual_agree_delay_ms) {
                continue;
            }
            std::uniform_real_distribution<double> dice(0.0, 1.0);
            int out_proposal_id = -1;
            std::string out_state;
            if (dice(*rng_) < config_->virtual_agree_prob) {
                agree(member_id, now_ms, &out_proposal_id, &out_state);
            } else {
                reject(member_id, now_ms, &out_proposal_id, &out_state);
            }
        }
    }

    // 3) 团到点自动完成：出发刻（date + depart_min，本地时区）已到即 completed。
    //    仅对「出发时刻之前成团」的团生效；成团时已过发车时刻的团（补录/演示场景）留给手动完成，
    //    否则演示中刚成立的团会瞬间消失（该细则同步写入 P2 的 state_machine 文档）。
    std::vector<int> due;
    for (const auto& [id, group] : groups_) {
        if (group.member_ids.empty()) continue;
        const Passenger* first = pool_->get(group.member_ids.front());
        if (first == nullptr) continue;
        const int64_t depart_ms = date_min_to_epoch_ms(first->win.date, group.depart_min);
        if (depart_ms < 0) continue;
        if (depart_ms >= group.formed_at_ms && now_ms >= depart_ms) due.push_back(id);
    }
    for (int id : due) {
        complete_group(id, now_ms);
    }
}

void StateMachine::reset() {
    proposals_.clear();
    groups_.clear();
    next_group_id_ = 1;
}

void StateMachine::dissolve_proposal(int proposal_id) {
    const auto it = proposals_.find(proposal_id);
    if (it == proposals_.end()) return;
    const std::vector<int> member_ids = it->second.proposal.member_ids;
    proposals_.erase(it);
    return_members_to_waiting(pool_, member_ids);
}

void StateMachine::dissolve_group(int group_id, int64_t now_ms) {
    const auto it = groups_.find(group_id);
    if (it == groups_.end()) return;
    const std::vector<int> member_ids = it->second.member_ids;
    groups_.erase(it);
    return_members_to_waiting(pool_, member_ids);
    events_->append("group_dissolved", {{"group_id", group_id}, {"member_ids", member_ids}},
                    now_ms);
}

int StateMachine::form_group(const ProposalRecord& record, int64_t now_ms) {
    Group group;
    group.id = next_group_id_++;
    group.member_ids = record.proposal.member_ids;
    group.depart_min = record.proposal.depart_min;
    group.formed_at_ms = now_ms;
    for (int id : group.member_ids) {
        pool_->set_status(id, Status::kGrouped, -1,
                          group.id);  // proposal_id 清空，group_id 记当前团
    }
    const int group_id = group.id;
    events_->append("group_formed",
                    {{"group_id", group.id},
                     {"member_ids", group.member_ids},
                     {"depart_min", group.depart_min}},
                    now_ms);
    groups_[group_id] = std::move(group);
    return group_id;
}

}  // namespace merging
