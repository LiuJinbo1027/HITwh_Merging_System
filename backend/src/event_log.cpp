#include "event_log.h"

#include <algorithm>

namespace merging {

int64_t EventLog::append(const std::string& type, const nlohmann::json& payload, int64_t ts_ms) {
    Event e;
    e.id = next_id_;
    e.type = type;
    e.payload = payload;
    e.ts_ms = ts_ms;
    if (events_.empty()) first_id_ = e.id;
    events_.push_back(std::move(e));
    ++next_id_;
    return events_.back().id;
}

EventLog::Slice EventLog::events_since(int64_t since_id, int64_t limit) const {
    Slice slice;
    slice.next_since_id = since_id;
    if (limit <= 0 || events_.empty()) return slice;
    // 落后于最早可用事件的客户端（如 reset 后仍拿着旧 since_id）：从当前首条开始补齐
    if (since_id < first_id_ - 1) since_id = first_id_ - 1;
    const int64_t start = since_id + 1 - first_id_;
    if (start >= static_cast<int64_t>(events_.size())) return slice;
    const int64_t end = std::min<int64_t>(static_cast<int64_t>(events_.size()), start + limit);
    slice.events.reserve(static_cast<size_t>(end - start));
    for (int64_t i = start; i < end; ++i) {
        slice.events.push_back(events_[static_cast<size_t>(i)]);
    }
    slice.next_since_id = slice.events.back().id;
    return slice;
}

void EventLog::clear() {
    events_.clear();
    first_id_ = next_id_;  // id 不倒退；下一条 append 成为新日志的首条
}

}  // namespace merging
