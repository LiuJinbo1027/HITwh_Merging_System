#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model.h"
#include "nlohmann/json.hpp"

namespace merging {

// 全局事件日志（contract 第 3 节）：event_id 全局单调递增，前端以 since_id 增量轮询。
// 实现（manual_A：vector + 自增 id）：追加 O(1)；since_id 查询利用 id
// 连续的性质直接定位下标，O(k)。 reset 后日志清空但 event_id 不倒退（演示语义：清空已有事件，客户端
// since_id 不失效）。
class EventLog {
   public:
    // 追加事件并分配事件 id；ts_ms 由调用方注入（假时钟测试依赖）
    int64_t append(const std::string& type, const nlohmann::json& payload, int64_t ts_ms);

    // since_id 之后最多 limit 条（升序）。
    // next_since_id：有新事件时为最后一条 id，否则回显 since_id（前端下次轮询直接使用）。
    struct Slice {
        int64_t next_since_id = 0;
        std::vector<Event> events;
    };
    Slice events_since(int64_t since_id, int64_t limit) const;

    // reset：清空日志；事件 id 保持单调（下一条继续从 next_id_ 分配）
    void clear();

    int64_t size() const { return static_cast<int64_t>(events_.size()); }
    int64_t last_id() const { return next_id_ - 1; }

   private:
    std::vector<Event> events_;  // events_[i].id == first_id_ + i（id 连续）
    int64_t first_id_ = 1;       // 当前首条事件的 id（日志为空时无意义）
    int64_t next_id_ = 1;        // 下一条事件的 id
};

}  // namespace merging
