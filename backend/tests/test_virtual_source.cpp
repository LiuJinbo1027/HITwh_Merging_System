#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "engine.h"
#include "model.h"
#include "pool.h"
#include "virtual_source.h"

using namespace merging;

namespace {

const std::string kDay = "2026-10-08";
constexpr int kMorningLo = VirtualGenerator::kMorningPeakStart;     // 360
constexpr int kMorningHi = VirtualGenerator::kMorningPeakEnd - 90;  // 480（留 90 分钟余量）
constexpr int kEveningLo = VirtualGenerator::kEveningPeakStart;     // 840
constexpr int kEveningHi = VirtualGenerator::kEveningPeakEnd - 90;  // 990

bool in_peak(int start) {
    return (start >= kMorningLo && start <= kMorningHi) ||
           (start >= kEveningLo && start <= kEveningHi);
}

// generate 后把新乘客快照出来（pool.add 顺序即返回的 id 顺序）
std::vector<Passenger> dump(const MatchPool& pool, const std::vector<int>& ids) {
    std::vector<Passenger> out;
    for (int id : ids) out.push_back(*pool.get(id));
    return out;
}

}  // namespace

// ---- 确定性 ----

TEST_CASE("固定种子：两次独立生成序列完全一致（可复现）") {
    MatchPool pool_a, pool_b;
    VirtualGenerator gen_a(&pool_a), gen_b(&pool_b);
    std::mt19937 rng_a(42), rng_b(42);
    const std::vector<int> ids_a = gen_a.generate(50, kDay, rng_a);
    const std::vector<int> ids_b = gen_b.generate(50, kDay, rng_b);
    REQUIRE(ids_a.size() == 50);
    REQUIRE(ids_b.size() == 50);
    for (size_t i = 0; i < ids_a.size(); ++i) {
        const Passenger& pa = *pool_a.get(ids_a[i]);
        const Passenger& pb = *pool_b.get(ids_b[i]);
        CHECK(pa.party_size == pb.party_size);
        CHECK(pa.gender == pb.gender);
        CHECK(pa.pref == pb.pref);
        CHECK(pa.win.start_min == pb.win.start_min);
        CHECK(pa.win.end_min == pb.win.end_min);
    }
}

TEST_CASE("不同种子：生成序列不同") {
    MatchPool pool_a, pool_b;
    VirtualGenerator gen_a(&pool_a), gen_b(&pool_b);
    std::mt19937 rng_a(42), rng_b(7);
    const auto a = dump(pool_a, gen_a.generate(50, kDay, rng_a));
    const auto b = dump(pool_b, gen_b.generate(50, kDay, rng_b));
    bool any_diff = false;
    for (size_t i = 0; i < a.size() && !any_diff; ++i) {
        if (a[i].win.start_min != b[i].win.start_min || a[i].party_size != b[i].party_size) {
            any_diff = true;
        }
    }
    CHECK(any_diff);
}

// ---- 字段合法性（FR-8）----

TEST_CASE("字段合法性：200 名虚拟乘客全部满足分布约束") {
    MatchPool pool;
    VirtualGenerator gen(&pool);
    std::mt19937 rng(42);
    const auto ps = dump(pool, gen.generate(200, kDay, rng));
    REQUIRE(ps.size() == 200);

    bool morning_seen = false, evening_seen = false;
    for (const Passenger& p : ps) {
        CHECK(p.party_size >= 1);
        CHECK(p.party_size <= 4);
        CHECK(p.win.date == kDay);
        CHECK(p.win.start_min >= 0);
        CHECK(p.win.end_min <= 1440);
        CHECK(p.win.start_min <= p.win.end_min);
        CHECK(in_peak(p.win.start_min));  // 高峰窗内（留 90 分钟余量保证重叠）
        CHECK(p.is_virtual);
        CHECK(p.status == Status::kWaiting);
        if (p.win.start_min < VirtualGenerator::kMorningPeakEnd) morning_seen = true;
        if (p.win.start_min >= VirtualGenerator::kEveningPeakStart) evening_seen = true;
    }
    CHECK(morning_seen);  // 双峰都有样本
    CHECK(evening_seen);
}

TEST_CASE("取值分布：性别与偏好均覆盖（200 样本）") {
    MatchPool pool;
    VirtualGenerator gen(&pool);
    std::mt19937 rng(42);
    const auto ps = dump(pool, gen.generate(200, kDay, rng));

    std::set<Gender> genders;
    std::set<GenderPref> prefs;
    for (const Passenger& p : ps) {
        genders.insert(p.gender);
        prefs.insert(p.pref);
    }
    CHECK(genders.size() == 2);  // male/female 均出现
    CHECK(prefs.size() == 3);    // none/female_only/male_only 均出现
}

TEST_CASE("count=0：返回空、不产生乘客") {
    MatchPool pool;
    VirtualGenerator gen(&pool);
    std::mt19937 rng(42);
    CHECK(gen.generate(0, kDay, rng).empty());
    CHECK(pool.size() == 0);
}

// ---- AC-2.3：虚拟乘客参与匹配后偏好 / 容量 / 时间窗全部满足 ----

TEST_CASE("AC-2.3 100 名虚拟乘客经引擎成团：提案全部合法且偏好满足") {
    MatchPool pool;
    VirtualGenerator gen(&pool);
    MatchEngine engine(&pool);
    std::mt19937 rng(42);
    gen.generate(100, kDay, rng);
    const auto proposals = engine.run_once(1000);
    REQUIRE_FALSE(proposals.empty());

    std::set<int> proposal_ids;
    for (const auto& prop : proposals) {
        CHECK(proposal_ids.insert(prop.id).second);

        int head = 0, max_start = -1, min_end = 1441;
        for (size_t i = 0; i < prop.member_ids.size(); ++i) {
            const Passenger* m = pool.get(prop.member_ids[i]);
            REQUIRE(m != nullptr);
            CHECK(m->status == Status::kProposed);
            CHECK(m->proposal_id == prop.id);
            head += m->party_size;
            max_start = std::max(max_start, m->win.start_min);
            min_end = std::min(min_end, m->win.end_min);

            // 性别偏好（契约语义：只约束「其他成员」）
            if (m->pref == GenderPref::kNone) continue;
            for (size_t j = 0; j < prop.member_ids.size(); ++j) {
                if (i == j) continue;
                const Passenger* other = pool.get(prop.member_ids[j]);
                if (m->pref == GenderPref::kFemaleOnly) CHECK(other->gender == Gender::kFemale);
                if (m->pref == GenderPref::kMaleOnly) CHECK(other->gender == Gender::kMale);
            }
        }
        CHECK(head <= 4);                     // 容量
        CHECK(head >= 2);                     // 成团下限
        CHECK(prop.depart_min == max_start);  // depart = max(start)
        CHECK(max_start <= min_end);          // 时间窗全体相交（Helly）
    }
}
