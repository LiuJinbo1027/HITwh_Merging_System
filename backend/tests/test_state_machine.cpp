#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "doctest.h"
#include "engine.h"
#include "event_log.h"
#include "model.h"
#include "pool.h"
#include "state_machine.h"

using namespace merging;

namespace {

const std::string kDay = "2026-10-08";

TimeWindow win(int start, int end, const std::string& date = kDay) {
    return TimeWindow{date, start, end};
}

// 状态机测试夹具：池 + 事件日志 + 配置 + 引擎 + 状态机（全假时钟，无真实等待）
struct Fixture {
    MatchPool pool;
    EventLog events;
    Config cfg;
    std::mt19937 rng{42};
    MatchEngine engine{&pool};
    StateMachine sm{&pool, &events, &cfg, &rng};

    Fixture() {
        engine.set_event_sink(
            [this](const Event& e) { events.append(e.type, e.payload, e.ts_ms); });
    }

    int add(int party_size, Gender gender, GenderPref pref, int start, int end,
            bool is_virtual = false, const std::string& date = kDay) {
        Passenger p;
        p.party_size = party_size;
        p.gender = gender;
        p.pref = pref;
        p.win = win(start, end, date);
        p.is_virtual = is_virtual;
        return pool.add(p);
    }

    // 引擎一轮 + 状态机登记（等价 Service::run_match_locked）
    std::vector<Proposal> match(int64_t now_ms) {
        std::vector<Proposal> proposals = engine.run_once(now_ms);
        sm.register_proposals(proposals);
        return proposals;
    }

    std::vector<Event> event_types_since(int64_t since_id) {
        return events.events_since(since_id, 1000).events;
    }

    int count_events(const std::string& type) {
        int n = 0;
        for (const Event& e : events.events_since(0, 1000).events) {
            if (e.type == type) ++n;
        }
        return n;
    }
};

}  // namespace

// ---- AC-2.2 主干：waiting → proposed → 全员 agree → grouped ----

TEST_CASE("AC-2.2 全员同意成团：状态 / proposal_id / group_id / 事件链") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
    const auto proposals = f.match(1000);
    REQUIRE(proposals.size() == 1);
    CHECK(f.pool.get(a)->status == Status::kProposed);
    CHECK(f.pool.get(a)->proposal_id == proposals[0].id);
    CHECK(f.events.events_since(0, 10).events.back().type == "proposed");

    int proposal_id = -1;
    std::string state;
    CHECK(f.sm.agree(a, 1100, &proposal_id, &state).code == 0);
    CHECK(state == "pending");  // 未全员同意
    CHECK(f.pool.get(a)->status == Status::kProposed);

    CHECK(f.sm.agree(b, 1200, &proposal_id, &state).code == 0);
    CHECK(state == "grouped");  // 全员同意
    CHECK(f.pool.get(a)->status == Status::kGrouped);
    CHECK(f.pool.get(b)->status == Status::kGrouped);
    CHECK(f.pool.get(a)->group_id == f.pool.get(b)->group_id);
    CHECK(f.pool.get(a)->proposal_id == -1);  // 成团后清空提案引用
    CHECK(f.sm.groups().size() == 1);
    CHECK(f.sm.groups().begin()->second.depart_min == 510);  // max(start)
    CHECK(f.sm.proposals().empty());

    CHECK(f.count_events("agreed") == 2);
    CHECK(f.count_events("group_formed") == 1);
    // 重复同意幂等：不报错、状态不变
    CHECK(f.sm.agree(b, 1300, &proposal_id, &state).code == 40901);  // 已 grouped → 状态冲突
}

TEST_CASE("AC-2.2 拒绝：提案解散、全体回池、可再次被匹配") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
    f.match(1000);

    int proposal_id = -1;
    std::string state;
    CHECK(f.sm.reject(b, 1100, &proposal_id, &state).code == 0);
    CHECK(state == "dissolved");
    CHECK(f.pool.get(a)->status == Status::kWaiting);
    CHECK(f.pool.get(b)->status == Status::kWaiting);
    CHECK(f.pool.get(a)->proposal_id == -1);
    CHECK(f.sm.proposals().empty());
    CHECK(f.count_events("rejected") == 1);

    const auto again = f.match(1200);  // 回池后可再次被匹配
    REQUIRE(again.size() == 1);
    CHECK(f.pool.get(a)->status == Status::kProposed);
}

// ---- AC-2.2 TTL 超时 ----

TEST_CASE("AC-2.2 提案 TTL 超时：proposal_expired、全体回池") {
    Fixture f;
    EngineConfig ec;
    ec.proposal_ttl_ms = 1000;
    f.engine.set_config(ec);
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
    const auto proposals = f.match(5000);  // deadline = 6000
    REQUIRE(proposals.size() == 1);

    f.sm.tick(5999);
    CHECK(f.pool.get(a)->status == Status::kProposed);  // 未到期
    f.sm.tick(6000);
    CHECK(f.pool.get(a)->status == Status::kWaiting);
    CHECK(f.pool.get(b)->status == Status::kWaiting);
    CHECK(f.sm.proposals().empty());
    CHECK(f.count_events("proposal_expired") == 1);
    const Event e = f.events.events_since(0, 100).events.back();
    CHECK(e.payload["proposal_id"] == proposals[0].id);
}

// ---- AC-2.2 改时间三态 ----

TEST_CASE("AC-2.2 改时间（waiting）：字段更新并重入新桶") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    Passenger fields = *f.pool.get(a);
    fields.win = win(700, 800);
    CHECK(f.sm.update(a, fields, 1000).code == 0);
    CHECK(f.pool.get(a)->status == Status::kWaiting);
    CHECK(f.pool.get(a)->win.start_min == 700);
    CHECK(std::find(f.pool.neighbors(win(700, 800)).begin(), f.pool.neighbors(win(700, 800)).end(),
                    a) != f.pool.neighbors(win(700, 800)).end());  // 新桶可见
    CHECK(f.pool.neighbors(win(500, 600)).empty());                // 旧桶已移除
    CHECK(f.count_events("updated") == 1);
}

TEST_CASE("AC-2.2 改时间（proposed）：解散提案、其余回池、本人按新窗重入") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
    f.match(1000);
    Passenger fields = *f.pool.get(a);
    fields.win = win(900, 1000);
    CHECK(f.sm.update(a, fields, 1100).code == 0);
    CHECK(f.sm.proposals().empty());
    CHECK(f.pool.get(a)->status == Status::kWaiting);
    CHECK(f.pool.get(a)->win.start_min == 900);
    CHECK(f.pool.get(b)->status == Status::kWaiting);  // 其余成员回池
    CHECK(f.pool.get(b)->win.start_min == 510);        // 未改动
    CHECK(f.count_events("updated") == 1);
    CHECK(f.count_events("proposal_expired") == 0);  // 解散不算超时
}

TEST_CASE("AC-2.2 改时间（grouped）：解散团、其余回池、本人按新窗重入") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
    f.match(1000);
    int proposal_id = -1;
    std::string state;
    CHECK(f.sm.agree(a, 1100, &proposal_id, &state).code == 0);
    CHECK(f.sm.agree(b, 1200, &proposal_id, &state).code == 0);
    REQUIRE(f.sm.groups().size() == 1);

    Passenger fields = *f.pool.get(b);
    fields.win = win(900, 1000);
    CHECK(f.sm.update(b, fields, 1300).code == 0);
    CHECK(f.sm.groups().empty());
    CHECK(f.pool.get(b)->status == Status::kWaiting);
    CHECK(f.pool.get(a)->status == Status::kWaiting);  // 其余成员回池
    CHECK(f.count_events("group_dissolved") == 1);
}

// ---- AC-2.2 取消三态 ----

TEST_CASE("AC-2.2 取消：waiting / proposed / grouped 三态与终态保护") {
    SUBCASE("waiting 直接取消") {
        Fixture f;
        const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
        CHECK(f.sm.cancel(a, 1000).code == 0);
        CHECK(f.pool.get(a)->status == Status::kCancelled);
        CHECK(f.count_events("cancelled") == 1);
        CHECK(f.sm.cancel(a, 1100).code == 40901);  // 重复取消 → 状态冲突
    }
    SUBCASE("proposed 取消：提案解散、其余回池") {
        Fixture f;
        const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
        const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
        f.match(1000);
        CHECK(f.sm.cancel(a, 1100).code == 0);
        CHECK(f.pool.get(a)->status == Status::kCancelled);
        CHECK(f.pool.get(b)->status == Status::kWaiting);
        CHECK(f.sm.proposals().empty());
    }
    SUBCASE("grouped 取消：团解散、其余回池") {
        Fixture f;
        const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
        const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
        f.match(1000);
        int proposal_id = -1;
        std::string state;
        f.sm.agree(a, 1100, &proposal_id, &state);
        f.sm.agree(b, 1200, &proposal_id, &state);
        CHECK(f.sm.cancel(b, 1300).code == 0);
        CHECK(f.sm.groups().empty());
        CHECK(f.pool.get(b)->status == Status::kCancelled);
        CHECK(f.pool.get(a)->status == Status::kWaiting);
        CHECK(f.count_events("group_dissolved") == 1);
        CHECK(f.count_events("cancelled") == 1);
    }
    SUBCASE("未知乘客 → 40401") {
        Fixture f;
        int proposal_id = -1;
        std::string state;
        CHECK(f.sm.cancel(999, 1000).code == 40401);
        CHECK(f.sm.agree(999, 1000, &proposal_id, &state).code == 40401);
        CHECK(f.sm.reject(999, 1000, &proposal_id, &state).code == 40401);
    }
}

// ---- AC-2.2 虚拟乘客自动同意 ----

TEST_CASE("AC-2.2 虚拟乘客延迟后自动同意（prob=1.0）") {
    Fixture f;
    f.cfg.virtual_agree_delay_ms = 1000;
    f.cfg.virtual_agree_prob = 1.0;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600, /*is_virtual=*/true);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600, /*is_virtual=*/true);
    f.match(1000);

    f.sm.tick(1999);  // 未到思考延迟
    CHECK(f.pool.get(a)->status == Status::kProposed);
    f.sm.tick(2000);  // 延迟到 → 自动同意 → 全员同意成团
    CHECK(f.pool.get(a)->status == Status::kGrouped);
    CHECK(f.pool.get(b)->status == Status::kGrouped);
    CHECK(f.sm.groups().size() == 1);
    CHECK(f.count_events("agreed") == 2);
    CHECK(f.count_events("group_formed") == 1);
}

TEST_CASE("AC-2.2 虚拟乘客自动拒绝（prob=0.0）：提案解散回池") {
    Fixture f;
    f.cfg.virtual_agree_delay_ms = 1000;
    f.cfg.virtual_agree_prob = 0.0;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600, /*is_virtual=*/true);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600, /*is_virtual=*/true);
    f.match(1000);
    f.sm.tick(2000);
    CHECK(f.pool.get(a)->status == Status::kWaiting);
    CHECK(f.pool.get(b)->status == Status::kWaiting);
    CHECK(f.sm.proposals().empty());
    CHECK(f.count_events("rejected") == 1);
    CHECK(f.count_events("group_formed") == 0);
}

TEST_CASE("AC-2.2 虚拟 + 真实混合：虚拟先自动同意，真实同意后才成团") {
    Fixture f;
    f.cfg.virtual_agree_delay_ms = 1000;
    f.cfg.virtual_agree_prob = 1.0;
    const int v = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600, /*is_virtual=*/true);
    const int h = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600, /*is_virtual=*/false);
    f.match(1000);
    f.sm.tick(2000);  // 只有虚拟成员自动同意
    CHECK(f.pool.get(v)->status == Status::kProposed);
    CHECK(f.pool.get(h)->status == Status::kProposed);
    CHECK(f.sm.groups().empty());

    int proposal_id = -1;
    std::string state;
    CHECK(f.sm.agree(h, 2100, &proposal_id, &state).code == 0);
    CHECK(state == "grouped");  // 真人同意后全员齐 → 成团
    CHECK(f.sm.groups().size() == 1);
}

// ---- 完成：手动 / 到点自动 ----

TEST_CASE("手动 complete：成员置 completed、团移除、重复完成 40401") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 500, 600);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 510, 600);
    f.match(1000);
    int proposal_id = -1;
    std::string state;
    f.sm.agree(a, 1100, &proposal_id, &state);
    f.sm.agree(b, 1200, &proposal_id, &state);
    const int group_id = f.sm.groups().begin()->first;

    CHECK(f.sm.complete_group(group_id, 1300).code == 0);
    CHECK(f.pool.get(a)->status == Status::kCompleted);
    CHECK(f.pool.get(b)->status == Status::kCompleted);
    CHECK(f.pool.get(a)->group_id == -1);
    CHECK(f.sm.groups().empty());
    CHECK(f.count_events("completed") == 1);
    CHECK(f.sm.complete_group(group_id, 1400).code == 40401);
    CHECK(f.sm.complete_group(999, 1400).code == 40401);
}

TEST_CASE("到点自动完成：出发时刻前成团 → tick 到点即 completed") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 600, 700);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 610, 700);
    const int64_t depart_ms = date_min_to_epoch_ms(kDay, 610);
    REQUIRE(depart_ms > 0);
    const int64_t formed_at = depart_ms - 5000;  // 出发前成团
    f.match(formed_at);
    int proposal_id = -1;
    std::string state;
    f.sm.agree(a, formed_at, &proposal_id, &state);
    f.sm.agree(b, formed_at, &proposal_id, &state);
    REQUIRE(f.sm.groups().size() == 1);

    f.sm.tick(depart_ms - 1);
    CHECK(f.pool.get(a)->status == Status::kGrouped);  // 未到点
    f.sm.tick(depart_ms);
    CHECK(f.pool.get(a)->status == Status::kCompleted);
    CHECK(f.pool.get(b)->status == Status::kCompleted);
    CHECK(f.sm.groups().empty());
}

TEST_CASE("到点自动完成：成团时已过发车时刻的团保持进行中（等待手动完成）") {
    Fixture f;
    const int a = f.add(1, Gender::kMale, GenderPref::kNone, 600, 700);
    const int b = f.add(1, Gender::kFemale, GenderPref::kNone, 610, 700);
    const int64_t depart_ms = date_min_to_epoch_ms(kDay, 610);
    const int64_t formed_at = depart_ms + 1000;  // 出发后才成团（补录/演示场景）
    f.match(formed_at);
    int proposal_id = -1;
    std::string state;
    f.sm.agree(a, formed_at, &proposal_id, &state);
    f.sm.agree(b, formed_at, &proposal_id, &state);

    f.sm.tick(formed_at + 1000);
    CHECK(f.pool.get(a)->status == Status::kGrouped);  // 不自动消失，避免演示中被瞬间清空
    CHECK(f.sm.groups().size() == 1);
}

TEST_CASE("时间助手：date + minute → epoch（非法输入防御）") {
    CHECK(date_min_to_epoch_ms("2026-10-08", 0) > 0);
    CHECK(date_min_to_epoch_ms("2026-10-08", 1440) ==
          date_min_to_epoch_ms("2026-10-09", 0));        // 1440 归一化为次日 0 点
    CHECK(date_min_to_epoch_ms("2026-02-29", 0) == -1);  // 2026 非闰年
    CHECK(date_min_to_epoch_ms("2024-02-29", 0) > 0);    // 2024 闰年
    CHECK(date_min_to_epoch_ms("2026-13-01", 0) == -1);
    CHECK(date_min_to_epoch_ms("2026-1-01", 0) == -1);  // 位数不足
    CHECK(date_min_to_epoch_ms("2026-10-08", 1441) == -1);
    CHECK(epoch_ms_to_date(date_min_to_epoch_ms("2026-10-08", 600)) == "2026-10-08");
}
