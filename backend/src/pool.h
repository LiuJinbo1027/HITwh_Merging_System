#pragma once

#include <set>
#include <unordered_map>
#include <vector>

#include "model.h"
#include "time_bucket.h"

namespace merging {

// 匹配池：全体乘客的唯一持有者 + 两个等待乘客索引。
//  - TimeBucketIndex（桶索引）：窗口候选查询 candidates_in_window O(1) 有界；
//  - std::multiset 扫描线（按 start_min 排序）：waiting_ids() 按起点升序迭代。
// 乘客离开 waiting 时同步移出两个索引；引擎直接读写池内乘客。
class MatchPool {
   public:
    // 分配全局唯一自增 id（从 1 起），强制 status=waiting 并进入索引。
    int add(const Passenger& passenger);

    Passenger* get(int id);
    const Passenger* get(int id) const;
    bool contains(int id) const { return passengers_.count(id) > 0; }

    // 移出池（P2 状态机/reset 使用；P1 引擎不删乘客）
    void remove(int id);

    // 修改状态；跨 waiting 边界时同步桶索引与扫描线
    void set_status(int id, Status status, int proposal_id = -1, int group_id = -1);

    // 全部 waiting 乘客 id，按 (start_min, id) 升序（扫描线顺序）
    std::vector<int> waiting_ids() const;

    // 与 win 时间窗有交集的 waiting 乘客 id，升序
    std::vector<int> neighbors(const TimeWindow& win) const;

    int size() const { return static_cast<int>(passengers_.size()); }

   private:
    void add_to_waiting_indexes(const Passenger& p);
    void remove_from_waiting_indexes(const Passenger& p);

    std::unordered_map<int, Passenger> passengers_;
    TimeBucketIndex buckets_;
    std::multiset<std::pair<int, int>> waiting_order_;  // (start_min, id) 扫描线
    int next_id_ = 1;
};

}  // namespace merging
