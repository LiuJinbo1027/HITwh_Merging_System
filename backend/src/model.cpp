#include "model.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace merging {

namespace {

bool is_leap_year(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int days_in_month(int year, int month) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) return 29;
    return kDays[month - 1];
}

}  // namespace

bool parse_date(const std::string& date, int* year, int* month, int* day) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    int parts[3] = {0, 0, 0};
    const int kPos[3] = {0, 5, 8};
    const int kLen[3] = {4, 2, 2};
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < kLen[i]; ++k) {
            const char c = date[kPos[i] + k];
            if (c < '0' || c > '9') return false;
            parts[i] = parts[i] * 10 + (c - '0');
        }
    }
    const int y = parts[0], m = parts[1], d = parts[2];
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > days_in_month(y, m)) return false;
    *year = y;
    *month = m;
    *day = d;
    return true;
}

int64_t date_min_to_epoch_ms(const std::string& date, int minute_of_day) {
    int y = 0, m = 0, d = 0;
    if (!parse_date(date, &y, &m, &d)) return -1;
    if (minute_of_day < 0 || minute_of_day > 1440) return -1;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = minute_of_day / 60;
    tm.tm_min = minute_of_day % 60;
    tm.tm_isdst = -1;
    // 本地时区解释（与 system_clock epoch 同一时间轴）；minute=1440 由 mktime 归一化到次日 0 点
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return -1;
    return static_cast<int64_t>(t) * 1000;
}

std::string epoch_ms_to_date(int64_t now_ms) {
    const std::time_t t = static_cast<std::time_t>(now_ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

bool TimeWindow::overlaps(const TimeWindow& other) const {
    // 冻结规则：单日（跨日不支持），直接不视为可同车
    if (date != other.date) return false;
    return std::max(start_min, other.start_min) <= std::min(end_min, other.end_min);
}

int TimeWindow::depart_min_with(const TimeWindow& other) const {
    return std::max(start_min, other.start_min);
}

}  // namespace merging
