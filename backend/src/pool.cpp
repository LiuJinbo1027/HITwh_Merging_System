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
