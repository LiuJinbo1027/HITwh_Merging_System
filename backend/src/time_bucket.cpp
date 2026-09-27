#include "time_bucket.h"

#include <algorithm>

namespace merging {

void TimeBucketIndex::insert(int id, const TimeWindow& win) {
    for (int b = bucket_of(win.start_min); b <= bucket_of(win.end_min); ++b) {
        buckets_[b].insert(id);
    }
    wins_[id] = win;
}

void TimeBucketIndex::remove(int id, const TimeWindow& win) {
    for (int b = bucket_of(win.start_min); b <= bucket_of(win.end_min); ++b) {
        buckets_[b].erase(id);
    }
    wins_.erase(id);
}

std::vector<int> TimeBucketIndex::candidates_in_window(int start_min, int end_min) const {
    std::vector<int> out;
    std::unordered_set<int> seen;
    for (int b = bucket_of(start_min); b <= bucket_of(end_min); ++b) {
        for (int id : buckets_[b]) {
            if (!seen.insert(id).second) continue;
            const auto it = wins_.find(id);
            // 精确过滤：max(start) ≤ min(end)（同日性由插入侧保证，桶内窗口均为合法单日）
            if (it == wins_.end()) continue;
            const TimeWindow& w = it->second;
            if (std::max(w.start_min, start_min) <= std::min(w.end_min, end_min)) {
                out.push_back(id);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> TimeBucketIndex::candidates_in_window(const TimeWindow& win) const {
    return candidates_in_window(win.start_min, win.end_min);
}

}  // namespace merging
