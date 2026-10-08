#pragma once

#include <cstdint>

namespace merging {

// API 可配参数（contract 第 5 节），/api/config 即改即生效。
// 默认值即契约默认值；reset 时恢复本默认值。
struct Config {
    int64_t proposal_ttl_ms = 60000;        // 同意等待超时（FR-7）
    double virtual_agree_prob = 0.9;        // 虚拟乘客自动同意概率（拒绝以演示重匹配）
    int64_t virtual_agree_delay_ms = 2000;  // 虚拟乘客同意前思考延迟
    int64_t auto_match_interval_ms = 5000;  // 自动匹配周期（0=关闭）
    int64_t stream_interval_ms = 3000;      // 虚拟流生成周期
    int stream_batch_size = 5;              // 每批生成人数
};

}  // namespace merging
