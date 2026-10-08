#include "pool.h"

#include <algorithm>

namespace merging {

int MatchPool::add(const Passenger& passenger) {
    Passenger p = passenger;
    p.id = next_id_++;
    p.status = Status::kWaiting;
    p.proposal_id = -1;
    p.group_id = -1;
    passengers_[p.id] = p;
    add_to_waiting_indexes(p);
    return p.id;
}

Passenger* MatchPool::get(int id) {
    const auto it = passengers_.find(id);
    return it == passengers_.end() ? nullptr : &it->second;
}

const Passenger* MatchPool::get(int id) const {
    const auto it = passengers_.find(id);
    return it == passengers_.end() ? nullptr : &it->second;
}

void MatchPool::remove(int id) {
    const auto it = passengers_.find(id);
    if (it == passengers_.end()) return;
    if (it->second.status == Status::kWaiting) {
        remove_from_waiting_indexes(it->second);
    }
    passengers_.erase(it);
}

void MatchPool::update(int id, const Passenger& fields) {
    Passenger* p = get(id);
    if (p == nullptr) return;
    const bool is_waiting = p->status == Status::kWaiting;
    if (is_waiting) remove_from_waiting_indexes(*p);
    p->party_size = fields.party_size;
    p->gender = fields.gender;
    p->pref = fields.pref;
    p->win = fields.win;
    if (is_waiting) add_to_waiting_indexes(*p);
}

void MatchPool::clear() {
    passengers_.clear();
    buckets_.clear();
    waiting_order_.clear();
    next_id_ = 1;
}

void MatchPool::set_status(int id, Status status, int proposal_id, int group_id) {
    Passenger* p = get(id);
    if (p == nullptr) return;
    const bool was_waiting = p->status == Status::kWaiting;
    p->status = status;
    p->proposal_id = proposal_id;
    p->group_id = group_id;
    const bool is_waiting = status == Status::kWaiting;
    if (was_waiting && !is_waiting) {
        remove_from_waiting_indexes(*p);
    } else if (!was_waiting && is_waiting) {
        add_to_waiting_indexes(*p);
    }
}

std::vector<int> MatchPool::waiting_ids() const {
    std::vector<int> ids;
    ids.reserve(waiting_order_.size());
    for (const auto& [start_min, id] : waiting_order_) {
        (void)start_min;
        ids.push_back(id);
    }
    return ids;
}

std::vector<int> MatchPool::active_ids() const {
    std::vector<int> ids;
    ids.reserve(passengers_.size());
    for (const auto& [id, p] : passengers_) {
        if (p.status == Status::kWaiting || p.status == Status::kProposed ||
            p.status == Status::kGrouped) {
            ids.push_back(id);
        }
    }
    // 契约 FR-10：按 start_min 升序，同起点按 id 升序（与 waiting 扫描线排序键一致）
    std::sort(ids.begin(), ids.end(), [this](int a, int b) {
        const Passenger* pa = get(a);
        const Passenger* pb = get(b);
        if (pa->win.start_min != pb->win.start_min) {
            return pa->win.start_min < pb->win.start_min;
        }
        return pa->id < pb->id;
    });
    return ids;
}

std::vector<int> MatchPool::neighbors(const TimeWindow& win) const {
    std::vector<int> ids = buckets_.candidates_in_window(win);
    // 桶索引只收 waiting 乘客；双保险过滤一次状态
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [&](int id) {
                                 const Passenger* p = get(id);
                                 return p == nullptr || p->status != Status::kWaiting;
                             }),
              ids.end());
    return ids;
}

void MatchPool::add_to_waiting_indexes(const Passenger& p) {
    buckets_.insert(p.id, p.win);
    waiting_order_.insert({p.win.start_min, p.id});
}

void MatchPool::remove_from_waiting_indexes(const Passenger& p) {
    buckets_.remove(p.id, p.win);
    waiting_order_.erase({p.win.start_min, p.id});
}

}  // namespace merging
