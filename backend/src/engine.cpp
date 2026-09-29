#include "engine.h"

#include <algorithm>
#include <unordered_set>

namespace merging {

MatchEngine::MatchEngine(MatchPool* pool, EngineConfig cfg) : pool_(pool), cfg_(cfg) {}

std::vector<Proposal> MatchEngine::run_once(int64_t now_ms) {
    std::vector<Proposal> proposals;

    // 种子优先级：party_size 降序 → start_min 升序 → id 升序（保证确定性）
    std::vector<int> seeds = pool_->waiting_ids();
    std::sort(seeds.begin(), seeds.end(), [&](int a, int b) {
        const Passenger& pa = *pool_->get(a);
        const Passenger& pb = *pool_->get(b);
        if (pa.party_size != pb.party_size) return pa.party_size > pb.party_size;
        if (pa.win.start_min != pb.win.start_min) return pa.win.start_min < pb.win.start_min;
        return pa.id < pb.id;
    });

    std::unordered_set<int> tried;  // 本轮组不成团的种子，防死循环
    for (int seed_id : seeds) {
        if (tried.count(seed_id) > 0) continue;
        const Passenger* seed = pool_->get(seed_id);
        // 已被本轮先形成的提案带走 → 不再是 waiting
        if (seed == nullptr || seed->status != Status::kWaiting) continue;

        std::vector<int> member_ids{seed_id};
        int seats_left = cfg_.vehicle_capacity - seed->party_size;
        int max_start = seed->win.start_min;
        int min_end = seed->win.end_min;

        if (seats_left > 0) {
            // 候选：与种子窗口重叠的 waiting 乘客，按 party_size 降序、start_min 升序
            std::vector<int> cands = pool_->neighbors(seed->win);
            std::sort(cands.begin(), cands.end(), [&](int a, int b) {
                const Passenger& pa = *pool_->get(a);
                const Passenger& pb = *pool_->get(b);
                if (pa.party_size != pb.party_size) return pa.party_size > pb.party_size;
                if (pa.win.start_min != pb.win.start_min)
                    return pa.win.start_min < pb.win.start_min;
                return pa.id < pb.id;
            });
            for (int cand_id : cands) {
                if (cand_id == seed_id) continue;
                const Passenger* c = pool_->get(cand_id);
                if (c == nullptr || c->status != Status::kWaiting) continue;
                if (c->party_size > seats_left) continue;  // (c) 容量
                // (a) 时间窗 Helly 性质：只需维护团内 max(start) 与 min(end)，O(1)
                if (std::max(max_start, c->win.start_min) > std::min(min_end, c->win.end_min)) {
                    continue;
                }
                // (b) 性别偏好朴素重验（全团 × 候选）
                if (!gender_compatible(member_ids, cand_id)) continue;
                member_ids.push_back(cand_id);
                seats_left -= c->party_size;
                max_start = std::max(max_start, c->win.start_min);
                min_end = std::min(min_end, c->win.end_min);
                if (seats_left == 0) break;
            }
        }

        int head_count = 0;
        for (int id : member_ids) head_count += pool_->get(id)->party_size;
        if (head_count >= cfg_.min_group_size) {
            Proposal p;
            p.id = next_proposal_id_++;
            p.member_ids = member_ids;
            p.depart_min = max_start;
            p.deadline_ms = now_ms + cfg_.proposal_ttl_ms;
            for (int id : member_ids) {
                pool_->set_status(id, Status::kProposed, p.id);
            }
            proposals.push_back(p);
            emit_proposed(p, now_ms);
        } else {
            tried.insert(seed_id);  // 本轮组不成：不阻塞后续种子
        }
    }
    return proposals;
}

bool MatchEngine::gender_compatible(const std::vector<int>& member_ids, int candidate_id) const {
    const Passenger* cand = pool_->get(candidate_id);
    if (cand == nullptr) return false;
    // 契约语义（contract §1）：偏好只约束「同车其他成员」，不含本人。
    // 入团不变量：现有成员之间的偏好两两已满足（每次加入都做过本双向校验），
    // 因此只校验候选 x 的两个方向，无需团内重验（单元素团平凡满足）。
    //   (1) x 的偏好约束现有成员（现有成员是 x 的「其他成员」）
    if (cand->pref == GenderPref::kFemaleOnly) {
        for (int mid : member_ids) {
            if (pool_->get(mid)->gender != Gender::kFemale) return false;
        }
    } else if (cand->pref == GenderPref::kMaleOnly) {
        for (int mid : member_ids) {
            if (pool_->get(mid)->gender != Gender::kMale) return false;
        }
    }
    //   (2) 现有成员的偏好约束 x（x 是他们新的「其他成员」）
    for (int mid : member_ids) {
        const Passenger* p = pool_->get(mid);
        if (p->pref == GenderPref::kFemaleOnly && cand->gender != Gender::kFemale) return false;
        if (p->pref == GenderPref::kMaleOnly && cand->gender != Gender::kMale) return false;
    }
    return true;
}

void MatchEngine::emit_proposed(const Proposal& p, int64_t now_ms) {
    if (!sink_) return;
    Event e;
    e.id = next_event_id_++;
    e.type = "proposed";
    e.payload = {{"proposal_id", p.id}, {"member_ids", p.member_ids}, {"depart_min", p.depart_min}};
    e.ts_ms = now_ms;
    sink_(e);
}

}  // namespace merging
