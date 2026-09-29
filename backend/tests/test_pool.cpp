#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <string>
#include <vector>

#include "doctest.h"
#include "model.h"
#include "pool.h"

using namespace merging;

namespace {

const std::string kDay = "2026-09-28";

Passenger make(int party_size, Gender gender, GenderPref pref, int start, int end) {
    Passenger p;
    p.party_size = party_size;
    p.gender = gender;
    p.pref = pref;
    p.win = TimeWindow{kDay, start, end};
    p.is_virtual = false;
    return p;
}

bool contains(const std::vector<int>& ids, int id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

}  // namespace

// ---- MatchPool（FR-10 非终态枚举，P1 契约调整后补） ----

TEST_CASE("FR-10 active_ids：返回 waiting/proposed/grouped，排除终态") {
    MatchPool pool;
    const int w1 = pool.add(make(1, Gender::kMale, GenderPref::kNone, 600, 660));
    const int p1 = pool.add(make(2, Gender::kFemale, GenderPref::kNone, 400, 500));
    const int g1 = pool.add(make(3, Gender::kFemale, GenderPref::kNone, 300, 400));
    const int c1 = pool.add(make(1, Gender::kMale, GenderPref::kNone, 500, 560));
    const int c2 = pool.add(make(1, Gender::kFemale, GenderPref::kNone, 700, 760));

    pool.set_status(p1, Status::kProposed, 7);
    pool.set_status(g1, Status::kGrouped, -1, 3);
    pool.set_status(c1, Status::kCancelled);
    pool.set_status(c2, Status::kCompleted, -1, 9);

    const auto ids = pool.active_ids();
    CHECK(ids.size() == 3);
    CHECK(contains(ids, w1));
    CHECK(contains(ids, p1));
    CHECK(contains(ids, g1));
    CHECK_FALSE(contains(ids, c1));  // cancelled 不出现在列表
    CHECK_FALSE(contains(ids, c2));  // completed 不出现在列表
}

TEST_CASE("FR-10 active_ids：按 start_min 升序，同起点按 id 升序") {
    MatchPool pool;
    // 插入顺序故意打乱；状态不同不影响排序键
    const int a = pool.add(make(1, Gender::kMale, GenderPref::kNone, 800, 860));
    const int b = pool.add(make(1, Gender::kMale, GenderPref::kNone, 300, 360));
    const int c = pool.add(make(1, Gender::kMale, GenderPref::kNone, 300, 400));  // 与 b 同起点
    const int d = pool.add(make(1, Gender::kMale, GenderPref::kNone, 550, 610));
    pool.set_status(a, Status::kGrouped, -1, 1);

    CHECK(pool.active_ids() == std::vector<int>{b, c, d, a});
}

TEST_CASE("FR-10 active_ids：状态流转实时反映（含 remove）") {
    MatchPool pool;
    const int x = pool.add(make(1, Gender::kMale, GenderPref::kNone, 500, 600));
    CHECK(pool.active_ids() == std::vector<int>{x});

    pool.set_status(x, Status::kProposed, 1);
    CHECK(pool.active_ids() == std::vector<int>{x});  // proposed 仍在列

    pool.set_status(x, Status::kCompleted, -1, 2);
    CHECK(pool.active_ids().empty());  // 终态即时移除

    pool.set_status(x, Status::kWaiting);
    CHECK(pool.active_ids() == std::vector<int>{x});   // 回池重现
    CHECK(pool.waiting_ids() == std::vector<int>{x});  // waiting 索引同步恢复

    pool.remove(x);
    CHECK(pool.active_ids().empty());  // 移除后不再出现
}
