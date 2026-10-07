#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "model.h"
#include "time_bucket.h"

using namespace merging;

namespace {

const std::string kDay = "2026-09-28";

TimeWindow win(int start, int end, const std::string& date = kDay) {
    return TimeWindow{date, start, end};
}

bool contains(const std::vector<int>& ids, int id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

// ---- TimeWindow（manual_A：单测最先覆盖） ----

TEST_CASE("TimeWindow::overlaps：重叠/相接/不相交/跨日") {
    CHECK(win(500, 600).overlaps(win(520, 540)));                      // 完全包含
    CHECK(win(520, 540).overlaps(win(500, 600)));                      // 反向
    CHECK(win(500, 600).overlaps(win(600, 650)));                      // 端点相接 max==min 算可同车
    CHECK(win(600, 650).overlaps(win(500, 600)));                      // 反向端点相接
    CHECK(win(500, 600).overlaps(win(500, 600)));                      // 自身
    CHECK_FALSE(win(500, 540).overlaps(win(550, 600)));                // 不相交
    CHECK_FALSE(win(550, 600).overlaps(win(500, 540)));                // 反向不相交
    CHECK_FALSE(win(500, 600).overlaps(win(500, 600, "2026-09-29")));  // 跨日不支持
}

TEST_CASE("TimeWindow::depart_min_with") {
    CHECK(win(500, 600).depart_min_with(win(520, 580)) == 520);
    CHECK(win(520, 580).depart_min_with(win(500, 600)) == 520);
    CHECK(win(500, 600).depart_min_with(win(500, 600)) == 500);
    CHECK(win(400, 500).depart_min_with(win(430, 470)) == 430);
}

// ---- TimeBucketIndex（AC-1.1） ----

TEST_CASE("时间桶：单桶插入与查询") {
    TimeBucketIndex idx;
    CHECK(idx.empty());
    idx.insert(1, win(500, 509));
    idx.insert(2, win(505, 515));
    CHECK(idx.size() == 2);
    // 查询 [500,509]：两个乘客都与之重叠
    CHECK(idx.candidates_in_window(500, 509) == std::vector<int>{1, 2});
    // 查询 [510,515]：1 的 end=509 < 510，只有 2
    CHECK(idx.candidates_in_window(510, 515) == std::vector<int>{2});
    // 查询 [516,520]：都不重叠
    CHECK(idx.candidates_in_window(516, 520).empty());
}

TEST_CASE("时间桶：跨桶边界候选查询（AC-1.1 重点）") {
    TimeBucketIndex idx;
    // 覆盖桶 55~61 的宽窗口乘客
    idx.insert(3, win(550, 610));
    // 查询窗口横跨桶 59/60（595-605）：应命中
    CHECK(idx.candidates_in_window(595, 605) == std::vector<int>{3});
    // 查询完全落在乘客窗口之外（611 起）：桶内候选被精确过滤
    CHECK(idx.candidates_in_window(611, 620).empty());
    CHECK(idx.candidates_in_window(540, 549).empty());
    // 查询恰在端点：max(550,610)==min(610,650)==610 相接可同车
    CHECK(idx.candidates_in_window(610, 650) == std::vector<int>{3});
    CHECK(idx.candidates_in_window(540, 550) == std::vector<int>{3});
}

TEST_CASE("时间桶：端点 0 与 1440 归桶") {
    TimeBucketIndex idx;
    idx.insert(4, win(1435, 1440));  // end=1440 归入末桶
    idx.insert(5, win(0, 5));        // start=0 归入首桶
    CHECK(idx.candidates_in_window(1430, 1440) == std::vector<int>{4});
    CHECK(idx.candidates_in_window(0, 3) == std::vector<int>{5});
    // 首桶内不重叠的假阳性被精确过滤
    CHECK(idx.candidates_in_window(6, 10).empty());
    // 跨整天的查询同时命中首尾
    CHECK(idx.candidates_in_window(0, 1440) == std::vector<int>{4, 5});
}

TEST_CASE("时间桶：桶内假阳性精确过滤") {
    TimeBucketIndex idx;
    idx.insert(6, win(595, 596));  // 只占桶 59
    // 查询 [585,594] 覆盖桶 58/59，桶命中但区间不重叠 → 过滤
    CHECK(idx.candidates_in_window(585, 594).empty());
    // 边界相接则保留
    CHECK(idx.candidates_in_window(594, 595) == std::vector<int>{6});
}

TEST_CASE("时间桶：删除只影响本人") {
    TimeBucketIndex idx;
    idx.insert(1, win(500, 509));
    idx.insert(2, win(505, 515));
    idx.insert(3, win(550, 610));
    idx.remove(1, win(500, 509));
    CHECK(idx.size() == 2);
    CHECK(idx.candidates_in_window(500, 509) == std::vector<int>{2});
    CHECK(idx.candidates_in_window(550, 610) == std::vector<int>{3});
    // 删除不存在的 id：无副作用、不崩溃
    idx.remove(99, win(0, 10));
    CHECK(idx.size() == 2);
    // 同 id 换窗口重插
    idx.insert(1, win(700, 709));
    CHECK(idx.candidates_in_window(700, 709) == std::vector<int>{1});
    CHECK(idx.candidates_in_window(500, 509) == std::vector<int>{2});
}

TEST_CASE("时间桶：多乘客结果升序去重") {
    TimeBucketIndex idx;
    idx.insert(10, win(500, 600));
    idx.insert(8, win(480, 520));
    idx.insert(9, win(490, 510));
    const auto ids = idx.candidates_in_window(495, 505);
    CHECK(ids == std::vector<int>{8, 9, 10});
}

TEST_CASE("时间桶：宽窗口覆盖多桶") {
    TimeBucketIndex idx;
    idx.insert(7, win(500, 620));  // 覆盖桶 50~62
    CHECK(idx.candidates_in_window(519, 521) == std::vector<int>{7});
    CHECK(idx.candidates_in_window(621, 630).empty());  // 桶 62 候选被精确过滤
    CHECK(idx.candidates_in_window(499, 500) == std::vector<int>{7});
    CHECK(idx.candidates_in_window(400, 499).empty());  // 桶 49 以外不命中
}
