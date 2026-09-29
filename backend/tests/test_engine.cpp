#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <chrono>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "engine.h"
#include "model.h"
#include "pool.h"

using namespace merging;

namespace {

const std::string kDay = "2026-09-28";

TimeWindow win(int start, int end, const std::string& date = kDay) {
    return TimeWindow{date, start, end};
}

int add(MatchPool& pool, int party_size, Gender gender, GenderPref pref, int start, int end) {
    Passenger p;
    p.party_size = party_size;
    p.gender = gender;
    p.pref = pref;
    p.win = win(start, end);
    return pool.add(p);
}

bool has(const std::vector<int>& ids, int id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// 对提案做全局合法性复查：时间窗 Helly / 性别偏好 / 容量 / depart_min / 状态与 proposal_id / id
// 唯一
struct CheckResult {
    bool depart_correct = true;
    bool windows_overlap = true;
    bool gender_ok = true;
    bool capacity_ok = true;
    bool head_ok = true;
    bool proposed_ok = true;
    bool ids_unique = true;
};

CheckResult validate(const std::vector<Proposal>& proposals, const MatchPool& pool,
                     int min_group_size = 2) {
    CheckResult r;
    std::set<int> proposal_ids;
    for (const auto& p : proposals) {
        if (!proposal_ids.insert(p.id).second) r.ids_unique = false;
        int head = 0;
        int max_start = -1;
        int min_end = 1441;
        for (int id : p.member_ids) {
            const Passenger* m = pool.get(id);
            if (m == nullptr) {
                r.proposed_ok = false;
                continue;
            }
            head += m->party_size;
            max_start = std::max(max_start, m->win.start_min);
            min_end = std::min(min_end, m->win.end_min);
            if (m->status != Status::kProposed || m->proposal_id != p.id) r.proposed_ok = false;
        }
        if (p.depart_min != max_start) r.depart_correct = false;
        if (max_start > min_end) r.windows_overlap = false;  // Helly：两两相交 ⇒ 全体相交
        if (head > 4) r.capacity_ok = false;
        if (head < min_group_size) r.head_ok = false;
        // 性别偏好（契约语义：只约束「其他成员」，不含本人——contract §1）
        for (int id : p.member_ids) {
            const Passenger* m = pool.get(id);
            if (m == nullptr) continue;
            if (m->pref != GenderPref::kFemaleOnly && m->pref != GenderPref::kMaleOnly) continue;
            for (int other_id : p.member_ids) {
                if (other_id == id) continue;  // 本人不受自己偏好约束
                const Passenger* o = pool.get(other_id);
                if (o == nullptr) continue;
                if (m->pref == GenderPref::kFemaleOnly && o->gender != Gender::kFemale) {
                    r.gender_ok = false;
                }
                if (m->pref == GenderPref::kMaleOnly && o->gender != Gender::kMale) {
                    r.gender_ok = false;
                }
            }
        }
    }
    return r;
}

}  // namespace

// ---- AC-1.2 贪心正确性 ----

TEST_CASE("AC-1.2 固定输入 12 名乘客 → 3 个 4 人团") {
    MatchPool pool;
    MatchEngine engine(&pool);
    // 组3: 400/410/420/430 (end 500)；组1: 500/510/520/530 (end 600)；组2: 550/560/570/580 (end
    // 650) party_size 均为 1；6 男 6 女、均无偏好
    const int starts[12] = {500, 510, 520, 530, 550, 560, 570, 580, 400, 410, 420, 430};
    const int ends[12] = {600, 600, 600, 600, 650, 650, 650, 650, 500, 500, 500, 500};
    for (int i = 0; i < 12; ++i) {
        add(pool, 1, (i % 2 == 0) ? Gender::kMale : Gender::kFemale, GenderPref::kNone, starts[i],
            ends[i]);
    }
    const auto proposals = engine.run_once(1000);
    REQUIRE(proposals.size() == 3);

    int total_members = 0;
    std::set<int> departs;
    for (const auto& p : proposals) {
        CHECK(p.member_ids.size() == 4);  // 每个团 4 人
        total_members += static_cast<int>(p.member_ids.size());
        departs.insert(p.depart_min);
    }
    CHECK(total_members == 12);  // 12 人全部被提案
    // 组1/组2 有重叠，具体归属取决于种子顺序，集合断言最稳
    CHECK(departs == std::set<int>{430, 530, 580});

    const auto r = validate(proposals, pool);
    CHECK(r.depart_correct);  // 每个团 depart_min == max(成员 start_min)
    CHECK(r.windows_overlap);
    CHECK(r.gender_ok);
    CHECK(r.capacity_ok);
    CHECK(r.head_ok);
    CHECK(r.proposed_ok);
    CHECK(r.ids_unique);
}

// ---- AC-1.3 性别偏好穷举断言 ----

TEST_CASE("AC-1.3 female_only 乘客在任何提案中不与男性同车") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int f1 = add(pool, 1, Gender::kFemale, GenderPref::kFemaleOnly, 500, 600);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 510, 600);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 520, 600);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 530, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 2);
    for (const auto& p : proposals) {
        if (has(p.member_ids, f1)) {
            for (int id : p.member_ids) {
                CHECK(pool.get(id)->gender == Gender::kFemale);
            }
        }
    }
    const auto r = validate(proposals, pool);
    CHECK(r.gender_ok);
}

TEST_CASE("AC-1.3 male_only 乘客在任何提案中不与女性同车") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int m1 = add(pool, 1, Gender::kMale, GenderPref::kMaleOnly, 500, 600);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 510, 600);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 520, 600);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 530, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 2);
    for (const auto& p : proposals) {
        if (has(p.member_ids, m1)) {
            for (int id : p.member_ids) {
                CHECK(pool.get(id)->gender == Gender::kMale);
            }
        }
    }
    const auto r = validate(proposals, pool);
    CHECK(r.gender_ok);
}

TEST_CASE("AC-1.3 交叉偏好（女限男 + 男限女）互相满足可同车") {
    MatchPool pool;
    MatchEngine engine(&pool);
    // 契约语义：偏好只约束其他成员——她要求对方是男，他要求对方是女，恰好互相满足
    const int f = add(pool, 1, Gender::kFemale, GenderPref::kMaleOnly, 500, 600);
    const int m = add(pool, 1, Gender::kMale, GenderPref::kFemaleOnly, 510, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 1);
    CHECK(has(proposals[0].member_ids, f));
    CHECK(has(proposals[0].member_ids, m));
    const auto r = validate(proposals, pool);
    CHECK(r.gender_ok);
}

TEST_CASE("AC-1.3 偏好不含本人：男 + 限女 可与女性同车") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int m1 = add(pool, 1, Gender::kMale, GenderPref::kFemaleOnly, 500, 600);
    const int f1 = add(pool, 1, Gender::kFemale, GenderPref::kNone, 510, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 1);
    CHECK(has(proposals[0].member_ids, m1));
    CHECK(has(proposals[0].member_ids, f1));
    const auto r = validate(proposals, pool);
    CHECK(r.gender_ok);
}

TEST_CASE("AC-1.3 约束方向不因本人性别翻转：限女的男乘客不与男性同车") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int m1 = add(pool, 1, Gender::kMale, GenderPref::kFemaleOnly, 500, 600);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 510, 600);
    const auto proposals = engine.run_once(0);
    CHECK(proposals.empty());  // m1 要求其他成员为女，另一男性不满足；双方都无法成团
}

TEST_CASE("AC-1.3 混合偏好：female_only 与 male_only 各自按偏好成团") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int f1 = add(pool, 1, Gender::kFemale, GenderPref::kFemaleOnly, 500, 600);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 510, 600);
    const int m1 = add(pool, 1, Gender::kMale, GenderPref::kMaleOnly, 520, 600);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 530, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 2);
    for (const auto& p : proposals) {
        if (has(p.member_ids, f1)) {
            for (int id : p.member_ids) CHECK(pool.get(id)->gender == Gender::kFemale);
        }
        if (has(p.member_ids, m1)) {
            for (int id : p.member_ids) CHECK(pool.get(id)->gender == Gender::kMale);
        }
    }
    const auto r = validate(proposals, pool);
    CHECK(r.gender_ok);
}

// ---- 引擎行为 ----

TEST_CASE("成团人数下限：单人不成团；双人/四人 party 可单独成团") {
    {
        MatchPool pool;
        MatchEngine engine(&pool);
        const int a = add(pool, 1, Gender::kMale, GenderPref::kNone, 500, 600);
        CHECK(engine.run_once(0).empty());  // 1 人 < min_group_size
        CHECK(pool.get(a)->status == Status::kWaiting);
    }
    {
        MatchPool pool;
        MatchEngine engine(&pool);
        const int a = add(pool, 2, Gender::kMale, GenderPref::kNone, 500, 600);
        const auto proposals = engine.run_once(0);
        REQUIRE(proposals.size() == 1);  // 总人数 2 == min_group_size
        CHECK(proposals[0].member_ids == std::vector<int>{a});
        CHECK(proposals[0].depart_min == 500);
    }
    {
        MatchPool pool;
        MatchEngine engine(&pool);
        const int a = add(pool, 4, Gender::kMale, GenderPref::kNone, 500, 600);
        const auto proposals = engine.run_once(0);
        REQUIRE(proposals.size() == 1);  // 满车即走
        CHECK(proposals[0].member_ids == std::vector<int>{a});
    }
}

TEST_CASE("容量：party 3 带不动 party 2，只带走 party 1") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int p3 = add(pool, 3, Gender::kMale, GenderPref::kNone, 500, 600);
    const int p2 = add(pool, 2, Gender::kMale, GenderPref::kNone, 510, 600);
    const int p1 = add(pool, 1, Gender::kMale, GenderPref::kNone, 520, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 2);
    // 第一提案 {p3, p1}：p2 超容量被跳过；p2 随后单独成团
    CHECK(has(proposals[0].member_ids, p3));
    CHECK(has(proposals[0].member_ids, p1));
    CHECK_FALSE(has(proposals[0].member_ids, p2));
    CHECK(proposals[1].member_ids == std::vector<int>{p2});
    const auto r = validate(proposals, pool);
    CHECK(r.capacity_ok);
    CHECK(r.head_ok);
}

TEST_CASE("种子优先级：party_size 最大者优先做种子") {
    MatchPool pool;
    MatchEngine engine(&pool);
    const int a =
        add(pool, 3, Gender::kMale, GenderPref::kNone, 600, 700);  // party 最大、start 最晚
    const int b = add(pool, 2, Gender::kMale, GenderPref::kNone, 500, 600);
    const int c = add(pool, 1, Gender::kMale, GenderPref::kNone, 510, 600);
    const int d = add(pool, 1, Gender::kMale, GenderPref::kNone, 520, 600);
    const auto proposals = engine.run_once(0);
    REQUIRE(proposals.size() == 2);
    // party 3 的 a 优先做种子带走 c；b 与 d 成团（若按 start_min 优先做种子则会 b+c / a+d）
    for (const auto& p : proposals) {
        if (has(p.member_ids, a)) CHECK(has(p.member_ids, c));
        if (has(p.member_ids, b)) CHECK(has(p.member_ids, d));
    }
    const auto r = validate(proposals, pool);
    CHECK(r.capacity_ok);
    CHECK(r.head_ok);
}

TEST_CASE("种子防死循环：互不重叠窗口正常终止且可重复调用") {
    MatchPool pool;
    MatchEngine engine(&pool);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 500, 505);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 600, 605);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 700, 705);
    CHECK(engine.run_once(0).empty());
    CHECK(pool.size() == 3);
    // 再跑一轮：依旧空、不崩溃（本轮已尝试集合只在本轮生效）
    CHECK(engine.run_once(0).empty());
    for (int id : pool.waiting_ids()) {
        CHECK(pool.get(id)->status == Status::kWaiting);
    }
}

TEST_CASE("事件流：proposed 事件与提案一一对应") {
    MatchPool pool;
    MatchEngine engine(&pool);
    std::vector<Event> events;
    engine.set_event_sink([&](const Event& e) { events.push_back(e); });
    add(pool, 1, Gender::kMale, GenderPref::kNone, 400, 470);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 410, 470);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 500, 600);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 510, 600);
    add(pool, 1, Gender::kMale, GenderPref::kNone, 520, 600);
    add(pool, 1, Gender::kFemale, GenderPref::kNone, 530, 600);
    const auto proposals = engine.run_once(1000);
    REQUIRE(proposals.size() == 2);
    REQUIRE(events.size() == proposals.size());
    for (size_t i = 0; i < proposals.size(); ++i) {
        CHECK(events[i].type == "proposed");
        CHECK(events[i].ts_ms == 1000);
        CHECK(events[i].payload["proposal_id"] == proposals[i].id);
        CHECK(events[i].payload["depart_min"] == proposals[i].depart_min);
        CHECK(events[i].payload["member_ids"].size() == proposals[i].member_ids.size());
    }
    CHECK(events[0].id < events[1].id);  // event_id 全局单调递增
}

// ---- AC-1.4 效率 ----

TEST_CASE("AC-1.4 n=500 waiting 乘客 run_once < 200ms") {
    MatchPool pool;
    MatchEngine engine(&pool);
    std::mt19937 rng(42);  // 固定种子可复现
    std::uniform_int_distribution<int> party_dist(1, 4);
    std::uniform_int_distribution<int> start_dist(0, 1200);
    std::uniform_int_distribution<int> dur_dist(30, 240);
    for (int i = 0; i < 500; ++i) {
        const int s = start_dist(rng);
        const int e = std::min(s + dur_dist(rng), 1440);
        add(pool, party_dist(rng), (i % 2 == 0) ? Gender::kMale : Gender::kFemale,
            GenderPref::kNone, s, e);
    }
    const auto t0 = std::chrono::steady_clock::now();
    const auto proposals = engine.run_once(0);
    const auto elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0)
            .count();
    CHECK(elapsed_us < 200 * 1000);  // < 200ms（实测通常远小于该值）

    const auto r = validate(proposals, pool);
    CHECK(r.depart_correct);
    CHECK(r.windows_overlap);
    CHECK(r.gender_ok);
    CHECK(r.capacity_ok);
    CHECK(r.head_ok);
    CHECK(r.proposed_ok);
    CHECK(r.ids_unique);
}
