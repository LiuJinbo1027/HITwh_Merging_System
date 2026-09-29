#pragma once

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "model.h"

namespace merging {

// 时间桶索引：一天 1440 分钟按 10 分钟粒度切 144 桶。
// 乘客插入其时间窗覆盖的全部桶；窗口查询扫描查询窗覆盖的桶并做精确重叠过滤。
// 复杂度：insert/remove/candidates_in_window 均为 O(覆盖桶数) ≤ 144 = O(1)（有界常数）。
class TimeBucketIndex {
   public:
    static constexpr int kBucketMinutes = 10;
    static constexpr int kBucketCount = 144;  // 24h * 6

    TimeBucketIndex() = default;

    // 插入乘客 id（要求窗口合法：0 ≤ start_min ≤ end_min ≤ 1440）。
    void insert(int id, const TimeWindow& win);

    // 删除乘客 id；id 不存在或窗口与插入时不一致则只清理能匹配到的桶。
    void remove(int id, const TimeWindow& win);

    // 查询与 [start_min, end_min] 有交集（可同车）的全部乘客 id，升序去重。
    // 返回前做精确重叠过滤，桶粒度只负责缩小候选范围。
    std::vector<int> candidates_in_window(int start_min, int end_min) const;
    std::vector<int> candidates_in_window(const TimeWindow& win) const;

    int size() const { return static_cast<int>(wins_.size()); }
    bool empty() const { return wins_.empty(); }

   private:
    // 分钟 → 桶下标；1440 归入最后一桶（0-1439 正常，1440 是闭区间端点）
    static int bucket_of(int minute) { return std::min(std::max(minute / kBucketMinutes, 0), 143); }

    std::array<std::unordered_set<int>, kBucketCount> buckets_;
    std::unordered_map<int, TimeWindow> wins_;  // 精确重叠过滤用
};

}  // namespace merging
