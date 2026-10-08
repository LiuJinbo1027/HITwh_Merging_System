#pragma once

#include <random>
#include <string>
#include <vector>

#include "model.h"

namespace merging {

class MatchPool;

// 虚拟乘客生成器（FR-8）：分布集中机场出行高峰窗 + 性别偏好比例随机。
// 随机数由调用方注入（Service 持有固定种子的 std::mt19937）→ 同一 seed 输出可复现，
// 单测与演示都能得到确定性序列。
class VirtualGenerator {
   public:
    // 高峰窗（分钟）：早高峰 06:00-09:30、晚高峰 14:00-18:00，双峰各半
    static constexpr int kMorningPeakStart = 360;
    static constexpr int kMorningPeakEnd = 570;
    static constexpr int kEveningPeakStart = 840;
    static constexpr int kEveningPeakEnd = 1080;

    explicit VirtualGenerator(MatchPool* pool) : pool_(pool) {}

    // 批量生成 count 名虚拟乘客（date 为 "YYYY-MM-DD"），返回新 passenger_id（升序）。
    std::vector<int> generate(int count, const std::string& date, std::mt19937& rng);

   private:
    Passenger make_one(std::mt19937& rng, const std::string& date) const;

    MatchPool* pool_;
};

}  // namespace merging
