#include "model.h"

#include <algorithm>

namespace merging {

bool TimeWindow::overlaps(const TimeWindow& other) const {
    // 冻结规则：单日（跨日不支持），直接不视为可同车
    if (date != other.date) return false;
    return std::max(start_min, other.start_min) <= std::min(end_min, other.end_min);
}

int TimeWindow::depart_min_with(const TimeWindow& other) const {
    return std::max(start_min, other.start_min);
}

}  // namespace merging
