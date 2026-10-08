#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "doctest.h"
#include "service.h"

using namespace merging;

namespace {

const std::string kDay = "2026-10-08";

nlohmann::json pax_body(int party, const std::string& gender, const std::string& pref, int start,
                        int end, const std::string& date = kDay) {
    return {{"party_size", party}, {"gender", gender},   {"gender_preference", pref},
            {"date", date},        {"start_min", start}, {"end_min", end}};
}

// 测试环境：固定假时钟（Service 不碰真实时间，全部行为可控可测）
struct Env {
    int64_t now = 1700000000000;  // 固定起点（2023-11-14，远早于测试用日期）
    Service svc{[this] { return now; }, 42};

    int create(int party = 1, const std::string& gender = "male", const std::string& pref = "none",
               int start = 500, int end = 600) {
        const Result r = svc.create_passenger(pax_body(party, gender, pref, start, end));
        REQUIRE(r.code == 0);
        return r.data["passenger_id"].get<int>();
    }

    // 关掉自动匹配（多数用例要精确控制 tick 行为）
    void auto_match_off() { svc.set_config({{"auto_match_interval_ms", 0}}); }

    int count_events(const std::string& type) {
        const Result r = svc.events(0, 1000);
        int n = 0;
        for (const auto& e : r.data["events"]) {
            if (e["event_type"] == type) ++n;
        }
        return n;
    }

    // 池视图中查活跃乘客（终态乘客不在池视图中）
    nlohmann::json pax_json(int id) {
        const Result r = svc.pool();
        for (const auto& p : r.data["passengers"]) {
            if (p["passenger_id"] == id) return p;
        }
        return nullptr;
    }

    std::string pax_status(int id) {
        const nlohmann::json p = pax_json(id);
        return p.is_null() ? std::string("(absent)") : p["status"].get<std::string>();
    }
};

}  // namespace

// ---- AC-2.4 参数错误 40001 / 时间窗 40902 ----

TEST_CASE("AC-2.4 入参校验：40001（字段）与 40902（时间窗）") {
    Env e;
    CHECK(e.svc.create_passenger(pax_body(5, "male", "none", 500, 600)).code == 40001);  // 人数越界
    CHECK(e.svc.create_passenger(pax_body(0, "male", "none", 500, 600)).code == 40001);
    CHECK(e.svc.create_passenger(pax_body(1, "other", "none", 500, 600)).code ==
          40001);                                                                       // 性别非法
    CHECK(e.svc.create_passenger(pax_body(1, "male", "any", 500, 600)).code == 40001);  // 偏好非法
    CHECK(e.svc.create_passenger(pax_body(1, "male", "none", -1, 600)).code == 40001);  // 越界
    CHECK(e.svc.create_passenger(pax_body(1, "male", "none", 500, 1441)).code == 40001);
    CHECK(e.svc.create_passenger(pax_body(1, "male", "none", 700, 600)).code ==
          40902);  // start>end
    nlohmann::json missing = pax_body(1, "male", "none", 500, 600);
    missing.erase("end_min");
    CHECK(e.svc.create_passenger(missing).code == 40001);  // 缺字段
    CHECK(e.svc
              .create_passenger({{"party_size", 1},
                                 {"gender", "male"},
                                 {"gender_preference", "none"},
                                 {"date", "2026-2-01"},
                                 {"start_min", 500},
                                 {"end_min", 600}})
              .code == 40001);  // 日期格式非法

    // PUT 子集合并后再校验：只改 start_min 造成 start>end 同样 40902
    const int a = e.create(1, "male", "none", 500, 600);
    CHECK(e.svc.update_passenger(a, {{"start_min", 700}}).code == 40902);
    CHECK(e.svc.update_passenger(a, {{"start_min", 550}}).code == 0);
    CHECK(e.pax_json(a)["start_min"] == 550);
    CHECK(e.pax_json(a)["end_min"] == 600);  // 未提供的字段保持原值
}

// ---- AC-2.4 资源不存在 40401 ----

TEST_CASE("AC-2.4 未知资源：40401") {
    Env e;
    CHECK(e.svc.agree(999).code == 40401);
    CHECK(e.svc.reject(999).code == 40401);
    CHECK(e.svc.cancel_passenger(999).code == 40401);
    CHECK(e.svc.update_passenger(999, {{"start_min", 500}}).code == 40401);
    CHECK(e.svc.complete_group(999).code == 40401);
}

// ---- AC-2.4 状态冲突 40901 ----

TEST_CASE("AC-2.4 状态冲突：waiting 同意/拒绝、终态修改、重复取消 → 40901") {
    Env e;
    e.auto_match_off();
    const int a = e.create(1, "male", "none", 500, 600);
    CHECK(e.svc.agree(a).code == 40901);  // waiting 无提案可同意
    CHECK(e.svc.reject(a).code == 40901);

    CHECK(e.svc.cancel_passenger(a).code == 0);
    CHECK(e.svc.cancel_passenger(a).code == 40901);                        // 重复取消
    CHECK(e.svc.update_passenger(a, {{"start_min", 550}}).code == 40901);  // 终态不可改
    CHECK(e.svc.agree(a).code == 40901);

    const int b = e.create(1, "male", "none", 500, 600);
    const int c = e.create(1, "female", "none", 510, 600);
    REQUIRE(e.svc.trigger_match().data["proposals"].size() == 1);
    CHECK(e.svc.agree(b).code == 0);
    const Result grouped = e.svc.agree(c);
    REQUIRE(grouped.code == 0);
    REQUIRE(grouped.data["proposal_state"] == "grouped");
    const int group_id = e.svc.groups().data["groups"][0]["group_id"].get<int>();
    REQUIRE(e.svc.complete_group(group_id).code == 0);
    CHECK(e.svc.agree(b).code == 40901);  // 完成后不可再同意
    CHECK(e.svc.update_passenger(b, {{"party_size", 2}}).code == 40901);
}

// ---- AC-2.4 虚拟流重复启动 40903 ----

TEST_CASE("AC-2.4 虚拟流已在运行 → 40903；stop 后可重启") {
    Env e;
    e.auto_match_off();
    CHECK(e.svc.stream_start(nlohmann::json::object()).code == 0);
    CHECK(e.svc.stream_start(nlohmann::json::object()).code == 40903);
    CHECK(e.svc.stream_stop().code == 0);
    CHECK(e.svc.stream_start(nlohmann::json::object()).code == 0);  // 停后可重启
    CHECK(e.svc.stream_stop().code == 0);
}

// ---- 全流程 + 事件链（信封契约）----

TEST_CASE("全流程：注册 → trigger → 全员同意 → grouped → 手动完成 与事件链") {
    Env e;
    e.auto_match_off();
    const int a = e.create(1, "male", "none", 500, 600);
    const int b = e.create(1, "female", "none", 510, 600);

    const Result t = e.svc.trigger_match();
    REQUIRE(t.code == 0);
    REQUIRE(t.data["proposals"].size() == 1);
    const auto& prop = t.data["proposals"][0];
    CHECK(prop["member_ids"].size() == 2);
    CHECK(prop["depart_min"] == 510);
    CHECK(e.pax_status(a) == "proposed");

    const Result g1 = e.svc.agree(a);
    REQUIRE(g1.code == 0);
    CHECK(g1.data["proposal_state"] == "pending");
    const Result g2 = e.svc.agree(b);
    REQUIRE(g2.code == 0);
    CHECK(g2.data["proposal_state"] == "grouped");
    CHECK(e.pax_status(a) == "grouped");

    const Result gs = e.svc.groups();
    REQUIRE(gs.code == 0);
    REQUIRE(gs.data["groups"].size() == 1);
    const int group_id = gs.data["groups"][0]["group_id"].get<int>();
    CHECK(gs.data["groups"][0]["member_ids"].size() == 2);

    const Result done = e.svc.complete_group(group_id);
    REQUIRE(done.code == 0);
    CHECK(done.data["status"] == "completed");
    CHECK(e.svc.complete_group(group_id).code == 40401);  // 团已不存在
    CHECK(e.svc.pool().data["passengers"].empty());       // 终态乘客不在池视图
    CHECK(e.svc.groups().data["groups"].empty());

    CHECK(e.count_events("created") == 2);
    CHECK(e.count_events("proposed") == 1);
    CHECK(e.count_events("agreed") == 2);
    CHECK(e.count_events("group_formed") == 1);
    CHECK(e.count_events("completed") == 1);
}

// ---- 事件分页 ----

TEST_CASE("events：since_id 增量轮询 / limit 截断 / next_since_id 语义") {
    Env e;
    e.auto_match_off();
    e.create(1, "male", "none", 500, 600);
    e.create(1, "female", "none", 510, 600);
    e.create(1, "male", "none", 600, 700);

    const Result first = e.svc.events(0, 1000);
    REQUIRE(first.code == 0);
    const int64_t next = first.data["next_since_id"].get<int64_t>();
    CHECK(first.data["events"].size() >= 3);
    CHECK(first.data["events"].back()["event_id"] == next);  // next = 最后一条 id

    // 无新事件：回显 since_id，返回空
    const Result none = e.svc.events(next, 1000);
    CHECK(none.data["events"].empty());
    CHECK(none.data["next_since_id"] == next);

    // 恰好一条新事件：增量只返回它
    e.svc.cancel_passenger(1);
    const Result inc = e.svc.events(next, 1000);
    REQUIRE(inc.data["events"].size() == 1);
    CHECK(inc.data["events"][0]["event_type"] == "cancelled");
    CHECK(inc.data["next_since_id"].get<int64_t>() > next);

    // limit 截断：只取前 2 条，next 停在第二条
    const Result lim = e.svc.events(0, 2);
    REQUIRE(lim.data["events"].size() == 2);
    CHECK(lim.data["events"][0]["event_id"] == 1);
    CHECK(lim.data["next_since_id"] == lim.data["events"][1]["event_id"]);
}

// ---- 配置 ----

TEST_CASE("config：默认值 / PUT 子集更新 / 非法值 40001 / auto_match 开关事件") {
    Env e;
    const Result def = e.svc.get_config();
    REQUIRE(def.code == 0);
    CHECK(def.data["proposal_ttl_ms"] == 60000);
    CHECK(def.data["virtual_agree_prob"] == 0.9);
    CHECK(def.data["auto_match_interval_ms"] == 5000);
    CHECK(def.data["stream_batch_size"] == 5);

    const Result put = e.svc.set_config({{"proposal_ttl_ms", 1000}});
    REQUIRE(put.code == 0);
    CHECK(put.data["proposal_ttl_ms"] == 1000);
    CHECK(put.data["stream_batch_size"] == 5);  // 未提供字段不变

    CHECK(e.svc.set_config({{"proposal_ttl_ms", -1}}).code == 40001);
    CHECK(e.svc.set_config({{"stream_batch_size", 0}}).code == 40001);
    CHECK(e.svc.set_config({{"stream_interval_ms", 0}}).code == 40001);
    CHECK(e.svc.set_config({{"virtual_agree_prob", 1.5}}).code == 40001);
    CHECK(e.svc.set_config({{"virtual_agree_prob", "high"}}).code == 40001);
    CHECK(e.svc.get_config().data["proposal_ttl_ms"] == 1000);  // 失败不改配置

    CHECK(e.svc.set_config({{"auto_match_interval_ms", 0}}).code == 0);
    CHECK(e.count_events("auto_match_stopped") == 1);
    CHECK(e.svc.set_config({{"auto_match_interval_ms", 1000}}).code == 0);
    CHECK(e.count_events("auto_match_started") == 1);
}

TEST_CASE("配置即改即生效：PUT ttl=1000 后提案到期即回收") {
    Env e;
    e.auto_match_off();
    REQUIRE(e.svc.set_config({{"proposal_ttl_ms", 1000}}).code == 0);
    const int a = e.create(1, "male", "none", 500, 600);
    const int b = e.create(1, "female", "none", 510, 600);
    REQUIRE(e.svc.trigger_match().data["proposals"].size() == 1);
    CHECK(e.pax_status(a) == "proposed");

    e.now += 999;
    CHECK(e.pax_status(a) == "proposed");  // 未到期
    e.now += 1;
    CHECK(e.pax_status(a) == "waiting");  // 到期回收
    CHECK(e.pax_status(b) == "waiting");
    CHECK(e.count_events("proposal_expired") == 1);
}

TEST_CASE("auto_match：时钟推进到间隔后自动出提案") {
    Env e;
    const int64_t t0 = e.now;
    REQUIRE(e.svc.set_config({{"auto_match_interval_ms", 1000}}).code == 0);
    const int a = e.create(1, "male", "none", 500, 600);
    e.create(1, "female", "none", 510, 600);

    e.now = t0 + 999;
    CHECK(e.pax_status(a) == "waiting");  // 未到自动匹配点
    e.now = t0 + 1000;
    CHECK(e.pax_status(a) == "proposed");  // tick 内自动匹配
    CHECK(e.count_events("proposed") == 1);
}

// ---- 虚拟乘客端 ----

TEST_CASE("虚拟流：start 首批立即生成 / 到期追加 / stop 停止") {
    Env e;
    e.auto_match_off();
    const Result s = e.svc.stream_start({{"interval_ms", 1000}, {"batch_size", 3}});
    REQUIRE(s.code == 0);
    CHECK(e.svc.stats().data["pool_size"] == 3);  // 首批立即生成

    e.now += 999;
    CHECK(e.svc.stats().data["pool_size"] == 3);
    e.now += 1;
    CHECK(e.svc.stats().data["pool_size"] == 6);  // 到期追加一批

    CHECK(e.svc.stream_stop().code == 0);
    e.now += 10000;
    CHECK(e.svc.stats().data["pool_size"] == 6);  // 停止后不再生成
    CHECK(e.count_events("stream_started") == 1);
    CHECK(e.count_events("stream_stopped") == 1);
    CHECK(e.count_events("virtual_generated") == 2);
}

TEST_CASE("virtual_generate：count/date 校验与生成结果") {
    Env e;
    e.auto_match_off();
    CHECK(e.svc.virtual_generate({{"count", 0}}).code == 40001);
    CHECK(e.svc.virtual_generate({{"count", 1001}}).code == 40001);
    CHECK(e.svc.virtual_generate({{"count", "many"}}).code == 40001);
    CHECK(e.svc.virtual_generate({{"count", 3}, {"date", "2026-13-01"}}).code == 40001);
    CHECK(e.svc.virtual_generate({{"count", 3}, {"date", 20261008}}).code == 40001);

    const Result r = e.svc.virtual_generate({{"count", 3}, {"date", kDay}});
    REQUIRE(r.code == 0);
    REQUIRE(r.data["generated"].size() == 3);
    for (const auto& id : r.data["generated"]) {
        const nlohmann::json p = e.pax_json(id.get<int>());
        REQUIRE_FALSE(p.is_null());
        CHECK(p["is_virtual"] == true);
        CHECK(p["date"] == kDay);
        CHECK(p["status"] == "waiting");
    }
    CHECK(e.count_events("virtual_generated") == 1);
}

// ---- optimize（P2 等价贪心）----

TEST_CASE("optimize：P2 等价贪心，返回同形提案且完成登记") {
    Env e;
    e.auto_match_off();
    const int a = e.create(1, "male", "none", 500, 600);
    const int b = e.create(1, "female", "none", 510, 600);
    const Result o = e.svc.optimize();
    REQUIRE(o.code == 0);
    REQUIRE(o.data["proposals"].size() == 1);
    const auto& prop = o.data["proposals"][0];
    CHECK(prop.contains("proposal_id"));
    CHECK(prop.contains("member_ids"));
    CHECK(prop.contains("depart_min"));
    CHECK(e.pax_status(a) == "proposed");  // optimize 同样走登记流程
    CHECK(e.svc.agree(a).code == 0);
    CHECK(e.svc.agree(b).data["proposal_state"] == "grouped");
}

// ---- stats ----

TEST_CASE("stats：字段与语义（池大小 / 团数 / 平均人数 / 性别比 / 偏好满足率）") {
    Env e;
    e.auto_match_off();
    const int a = e.create(1, "male", "none", 500, 600);
    const int b = e.create(1, "female", "none", 510, 600);
    REQUIRE(e.svc.trigger_match().data["proposals"].size() == 1);
    CHECK(e.svc.agree(a).code == 0);
    CHECK(e.svc.agree(b).code == 0);

    const Result s = e.svc.stats();
    REQUIRE(s.code == 0);
    CHECK(s.data["pool_size"] == 2);  // proposed/grouped 计入非终态
    CHECK(s.data["group_count"] == 1);
    CHECK(s.data["avg_group_size"].get<double>() == doctest::Approx(2.0));
    CHECK(s.data["female_ratio"].get<double>() == doctest::Approx(0.5));
    CHECK(s.data["gender_pref_satisfied_ratio"].get<double>() ==
          doctest::Approx(1.0));  // 无偏好乘客 → 约定 1.0

    const int group_id = e.svc.groups().data["groups"][0]["group_id"].get<int>();
    REQUIRE(e.svc.complete_group(group_id).code == 0);
    const Result s2 = e.svc.stats();
    CHECK(s2.data["pool_size"] == 0);
    CHECK(s2.data["group_count"] == 0);
    CHECK(s2.data["avg_group_size"].get<double>() == doctest::Approx(0.0));
    CHECK(s2.data["female_ratio"].get<double>() == doctest::Approx(0.0));
}

// ---- reset ----

TEST_CASE("reset：清空池/团/流、配置回默认、乘客 id 重排、事件 id 保持单调") {
    Env e;
    e.auto_match_off();
    const int a = e.create(1, "male", "none", 500, 600);
    const int b = e.create(1, "female", "none", 510, 600);
    REQUIRE(e.svc.trigger_match().data["proposals"].size() == 1);
    CHECK(e.svc.agree(a).code == 0);
    CHECK(e.svc.agree(b).code == 0);
    REQUIRE(e.svc.set_config({{"proposal_ttl_ms", 1000}}).code == 0);
    REQUIRE(e.svc.stream_start(nlohmann::json::object()).code == 0);
    const int64_t last_before = e.svc.events(0, 1000).data["next_since_id"].get<int64_t>();

    REQUIRE(e.svc.reset().code == 0);
    CHECK(e.svc.pool().data["passengers"].empty());
    CHECK(e.svc.groups().data["groups"].empty());
    CHECK(e.svc.get_config().data["proposal_ttl_ms"] == 60000);  // 配置回默认

    // reset 后唯一事件是 reset；事件 id 不倒退（客户端 since_id 不失效）
    const Result ev = e.svc.events(0, 1000);
    REQUIRE(ev.data["events"].size() == 1);
    CHECK(ev.data["events"][0]["event_type"] == "reset");
    CHECK(ev.data["events"][0]["event_id"].get<int64_t>() > last_before);

    // 乘客 id 从 1 重新分配
    CHECK(e.create(1, "male", "none", 500, 600) == 1);

    // 流状态已清（否则重复 start 会 40903）
    CHECK(e.svc.stream_start(nlohmann::json::object()).code == 0);
    CHECK(e.svc.stream_stop().code == 0);
}
