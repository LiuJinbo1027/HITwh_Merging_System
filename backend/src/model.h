#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace merging {

// 性别（与 docs/contract.md 第 1 节一致）
enum class Gender { kMale, kFemale };

// 性别偏好：约束同车「其他成员」的性别，不含本人（contract §1，P1 裁定语义）
enum class GenderPref { kNone, kFemaleOnly, kMaleOnly };

// 乘客状态机（P2 实现转移，P1 只用 waiting/proposed）
enum class Status { kWaiting, kProposed, kGrouped, kCompleted, kCancelled };

// 时间窗：单日、单方向（市区→机场），start_min ≤ end_min，0-1440 分钟
struct TimeWindow {
    std::string date;  // "YYYY-MM-DD"
    int start_min = 0;
    int end_min = 0;

    // 可同车判定：同日且 max(start_min) ≤ min(end_min)（contract 全局约定 3）
    bool overlaps(const TimeWindow& other) const;

    // 成团发车时间：max(成员 start_min)
    int depart_min_with(const TimeWindow& other) const;
};

struct Passenger {
    int id = -1;
    int party_size = 1;  // 1-4 人
    Gender gender = Gender::kMale;
    GenderPref pref = GenderPref::kNone;
    TimeWindow win;
    Status status = Status::kWaiting;
    int proposal_id = -1;
    int group_id = -1;
    bool is_virtual = false;
};

struct Proposal {
    int id = -1;
    std::vector<int> member_ids;  // 团内乘客（party_size 之和 ≤ 4）
    int depart_min = 0;           // = max(成员 start_min)
    int64_t deadline_ms = 0;      // now + proposal_ttl_ms（超时解散）
    int64_t created_at_ms = 0;    // 提案生成时间（P2：虚拟乘客同意延迟计时基准）
};

struct Group {
    int id = -1;
    std::vector<int> member_ids;
    int depart_min = 0;
    int64_t formed_at_ms = 0;
};

// 事件流条目（contract 第 3 节；P2 EventLog 接管全局 id 分配与持久化）
struct Event {
    int64_t id = 0;
    std::string type;  // event_type 枚举之一
    nlohmann::json payload;
    int64_t ts_ms = 0;
};

// ---- 时间助手（P2 状态机到点自动完成 / 虚拟流默认日期）----

// 解析 "YYYY-MM-DD" 并做公历合法性校验（含闰年、月天数）；非法返回 false。
bool parse_date(const std::string& date, int* year, int* month, int* day);

// "YYYY-MM-DD" 当日第 minute_of_day 分钟（0-1440）→ 本地时区 epoch 毫秒。
// 供状态机与 system_clock 注入的 now_ms 直接比较；日期或分钟数非法返回 -1。
int64_t date_min_to_epoch_ms(const std::string& date, int minute_of_day);

// epoch 毫秒 → 本地日期 "YYYY-MM-DD"（虚拟乘客默认日期用）
std::string epoch_ms_to_date(int64_t now_ms);

}  // namespace merging
