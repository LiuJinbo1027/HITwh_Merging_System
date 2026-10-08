#include "service.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace merging {

namespace {

using nlohmann::json;

struct FieldError {
    int code = 40001;
    std::string message = "参数错误";
};

Result ok_result(json data) { return Result{0, "ok", std::move(data)}; }

Result error_result(int code, const std::string& message) { return Result{code, message, nullptr}; }

Result error_result(const FieldError& err) { return error_result(err.code, err.message); }

std::string gender_str(Gender gender) { return gender == Gender::kFemale ? "female" : "male"; }

std::string pref_str(GenderPref pref) {
    switch (pref) {
        case GenderPref::kFemaleOnly:
            return "female_only";
        case GenderPref::kMaleOnly:
            return "male_only";
        default:
            return "none";
    }
}

std::string status_str(Status status) {
    switch (status) {
        case Status::kProposed:
            return "proposed";
        case Status::kGrouped:
            return "grouped";
        case Status::kCompleted:
            return "completed";
        case Status::kCancelled:
            return "cancelled";
        default:
            return "waiting";
    }
}

json passenger_to_json(const Passenger& p) {
    return json{{"passenger_id", p.id},
                {"party_size", p.party_size},
                {"gender", gender_str(p.gender)},
                {"gender_preference", pref_str(p.pref)},
                {"date", p.win.date},
                {"start_min", p.win.start_min},
                {"end_min", p.win.end_min},
                {"status", status_str(p.status)},
                {"proposal_id", p.proposal_id < 0 ? json(nullptr) : json(p.proposal_id)},
                {"group_id", p.group_id < 0 ? json(nullptr) : json(p.group_id)},
                {"is_virtual", p.is_virtual}};
}

json proposal_to_json(const Proposal& p) {
    return json{{"proposal_id", p.id}, {"member_ids", p.member_ids}, {"depart_min", p.depart_min}};
}

// 解析请求中的乘客字段（contract §1）。require_all=true：缺任一字段 40001；
// false（PUT）：只覆盖请求中出现的字段，最后统一校验合并结果（start ≤ end 是合并后才成立的约束）。
bool parse_passenger_body(const json& body, bool require_all, Passenger* out, FieldError* err) {
    auto fail = [&](int code, const std::string& msg) {
        err->code = code;
        err->message = msg;
        return false;
    };
    const bool has_party = body.contains("party_size");
    const bool has_gender = body.contains("gender");
    const bool has_pref = body.contains("gender_preference");
    const bool has_date = body.contains("date");
    const bool has_start = body.contains("start_min");
    const bool has_end = body.contains("end_min");
    if (require_all && !(has_party && has_gender && has_pref && has_date && has_start && has_end)) {
        return fail(40001,
                    "缺少必填字段（party_size/gender/gender_preference/date/start_min/end_min）");
    }
    if (has_party) {
        if (!body["party_size"].is_number_integer()) return fail(40001, "party_size 必须为整数");
        const int v = body["party_size"].get<int>();
        if (v < 1 || v > 4) return fail(40001, "party_size 越界（1-4）");
        out->party_size = v;
    }
    if (has_gender) {
        if (!body["gender"].is_string()) return fail(40001, "gender 必须为字符串");
        const std::string v = body["gender"].get<std::string>();
        if (v == "male") {
            out->gender = Gender::kMale;
        } else if (v == "female") {
            out->gender = Gender::kFemale;
        } else {
            return fail(40001, "gender 非法（male|female）");
        }
    }
    if (has_pref) {
        if (!body["gender_preference"].is_string())
            return fail(40001, "gender_preference 必须为字符串");
        const std::string v = body["gender_preference"].get<std::string>();
        if (v == "none") {
            out->pref = GenderPref::kNone;
        } else if (v == "female_only") {
            out->pref = GenderPref::kFemaleOnly;
        } else if (v == "male_only") {
            out->pref = GenderPref::kMaleOnly;
        } else {
            return fail(40001, "gender_preference 非法（none|female_only|male_only）");
        }
    }
    if (has_date) {
        if (!body["date"].is_string()) return fail(40001, "date 必须为字符串");
        const std::string v = body["date"].get<std::string>();
        int year = 0, month = 0, day = 0;
        if (!parse_date(v, &year, &month, &day)) return fail(40001, "date 非法（YYYY-MM-DD）");
        out->win.date = v;
    }
    if (has_start) {
        if (!body["start_min"].is_number_integer()) return fail(40001, "start_min 必须为整数");
        out->win.start_min = body["start_min"].get<int>();
    }
    if (has_end) {
        if (!body["end_min"].is_number_integer()) return fail(40001, "end_min 必须为整数");
        out->win.end_min = body["end_min"].get<int>();
    }
    if (out->win.start_min < 0 || out->win.start_min > 1440 || out->win.end_min < 0 ||
        out->win.end_min > 1440) {
        return fail(40001, "时间窗越界（0-1440）");
    }
    if (out->win.start_min > out->win.end_min) {
        return fail(40902, "时间窗非法：start_min > end_min");
    }
    return true;
}

// 读取配置项（int 型），越界或类型错 → 40001
bool read_int_config(const json& body, const char* key, int64_t min, int64_t max, int64_t* out,
                     FieldError* err) {
    if (!body.contains(key)) return true;
    if (!body[key].is_number_integer()) {
        *err = {40001, std::string(key) + " 必须为整数"};
        return false;
    }
    const int64_t v = body[key].get<int64_t>();
    if (v < min || v > max) {
        *err = {40001, std::string(key) + " 越界（" + std::to_string(min) + "-" +
                           std::to_string(max) + "）"};
        return false;
    }
    *out = v;
    return true;
}

int64_t system_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace

Service::Service() : Service(system_now_ms, 42) {}

Service::Service(std::function<int64_t()> clock, uint32_t seed)
    : clock_(std::move(clock)),
      seed_(seed),
      rng_(seed),
      engine_(&pool_),
      sm_(&pool_, &events_, &config_, &rng_),
      generator_(&pool_) {
    engine_.set_event_sink([this](const Event& e) { events_.append(e.type, e.payload, e.ts_ms); });
    apply_engine_config_locked();
    next_auto_match_ms_ = now_ms() + config_.auto_match_interval_ms;
}

void Service::tick() {
    std::lock_guard<std::mutex> lock(mu_);
    tick_locked(now_ms());
}

// ---- 乘客 ----

Result Service::create_passenger(const json& body) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    Passenger p;
    FieldError err;
    if (!parse_passenger_body(body, /*require_all=*/true, &p, &err)) return error_result(err);
    const int id = pool_.add(p);
    events_.append("created", {{"passenger_id", id}}, now);
    return ok_result({{"passenger_id", id}, {"status", "waiting"}});
}

Result Service::update_passenger(int passenger_id, const json& body) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    const Passenger* p = pool_.get(passenger_id);
    if (p == nullptr) return error_result(40401, "乘客不存在");
    if (p->status == Status::kCancelled || p->status == Status::kCompleted) {
        return error_result(40901, "状态冲突：乘客已结束，不能修改");
    }
    Passenger merged = *p;  // 任意子集：从旧值出发覆盖请求出现的字段
    FieldError err;
    if (!parse_passenger_body(body, /*require_all=*/false, &merged, &err)) return error_result(err);
    const SmResult r = sm_.update(passenger_id, merged, now);
    if (r.code != 0) return error_result(r.code, r.message);
    return ok_result({{"passenger_id", passenger_id}});
}

Result Service::cancel_passenger(int passenger_id) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    const SmResult r = sm_.cancel(passenger_id, now);
    if (r.code != 0) return error_result(r.code, r.message);
    return ok_result({{"passenger_id", passenger_id}, {"status", "cancelled"}});
}

Result Service::agree(int passenger_id) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    int proposal_id = -1;
    std::string state;
    const SmResult r = sm_.agree(passenger_id, now, &proposal_id, &state);
    if (r.code != 0) return error_result(r.code, r.message);
    return ok_result({{"proposal_id", proposal_id}, {"proposal_state", state}});
}

Result Service::reject(int passenger_id) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    int proposal_id = -1;
    std::string state;
    const SmResult r = sm_.reject(passenger_id, now, &proposal_id, &state);
    if (r.code != 0) return error_result(r.code, r.message);
    return ok_result({{"proposal_id", proposal_id}, {"proposal_state", state}});
}

// ---- 匹配 ----

Result Service::trigger_match() {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    const std::vector<Proposal> proposals = run_match_locked(now);
    json arr = json::array();
    for (const auto& p : proposals) arr.push_back(proposal_to_json(p));
    return ok_result({{"proposals", arr}});
}

Result Service::optimize() {
    // FR-17（P4 弹性）：P2 先等价回退贪心，保证端点可用、前端可联调；P4 替换为
    // 可行组枚举 + 匈牙利最大权匹配（n>30 同样回退贪心）。
    return trigger_match();
}

Result Service::pool() {
    std::lock_guard<std::mutex> lock(mu_);
    tick_locked(now_ms());
    json arr = json::array();
    for (int id : pool_.active_ids()) arr.push_back(passenger_to_json(*pool_.get(id)));
    return ok_result({{"passengers", arr}});
}

Result Service::groups() {
    std::lock_guard<std::mutex> lock(mu_);
    tick_locked(now_ms());
    json arr = json::array();
    for (const auto& [group_id, group] : sm_.groups()) {
        arr.push_back({{"group_id", group.id},
                       {"member_ids", group.member_ids},
                       {"depart_min", group.depart_min},
                       {"formed_at_ms", group.formed_at_ms}});
    }
    return ok_result({{"groups", arr}});
}

// ---- 事件 / 统计 / 完成 ----

Result Service::events(int64_t since_id, int64_t limit) {
    std::lock_guard<std::mutex> lock(mu_);
    tick_locked(now_ms());
    const EventLog::Slice slice = events_.events_since(since_id, limit);
    json arr = json::array();
    for (const auto& e : slice.events) {
        arr.push_back({{"event_id", e.id},
                       {"event_type", e.type},
                       {"payload", e.payload},
                       {"ts_ms", e.ts_ms}});
    }
    return ok_result({{"next_since_id", slice.next_since_id}, {"events", arr}});
}

Result Service::stats() {
    std::lock_guard<std::mutex> lock(mu_);
    tick_locked(now_ms());
    const std::vector<int> active = pool_.active_ids();
    int female_count = 0;
    for (int id : active) {
        if (pool_.get(id)->gender == Gender::kFemale) ++female_count;
    }
    const double female_ratio =
        active.empty() ? 0.0 : static_cast<double>(female_count) / active.size();

    // 进行中团的平均每车人数（按 party_size 求和，满载=4）
    int group_head_count = 0;
    for (const auto& [group_id, group] : sm_.groups()) {
        for (int id : group.member_ids) group_head_count += pool_.get(id)->party_size;
    }
    const double avg_group_size =
        sm_.groups().empty() ? 0.0 : static_cast<double>(group_head_count) / sm_.groups().size();

    // 偏好满足率：proposed/grouped 中带偏好者为分母；其同车「其他成员」全部满足偏好者为分子。
    // 引擎按不变量保证满足，该指标用于演示/回归验证（分母为 0 时约定为 1.0）。
    int pref_total = 0, pref_satisfied = 0;
    auto check_members = [&](const std::vector<int>& member_ids) {
        for (size_t i = 0; i < member_ids.size(); ++i) {
            const Passenger* p = pool_.get(member_ids[i]);
            if (p == nullptr || p->pref == GenderPref::kNone) continue;
            ++pref_total;
            bool ok = true;
            for (size_t j = 0; j < member_ids.size() && ok; ++j) {
                if (i == j) continue;
                const Passenger* other = pool_.get(member_ids[j]);
                if (other == nullptr) continue;
                if (p->pref == GenderPref::kFemaleOnly && other->gender != Gender::kFemale)
                    ok = false;
                if (p->pref == GenderPref::kMaleOnly && other->gender != Gender::kMale) ok = false;
            }
            if (ok) ++pref_satisfied;
        }
    };
    for (const auto& [proposal_id, record] : sm_.proposals())
        check_members(record.proposal.member_ids);
    for (const auto& [group_id, group] : sm_.groups()) check_members(group.member_ids);

    return ok_result({{"pool_size", active.size()},
                      {"group_count", sm_.groups().size()},
                      {"avg_group_size", avg_group_size},
                      {"female_ratio", female_ratio},
                      {"gender_pref_satisfied_ratio",
                       pref_total == 0 ? 1.0 : static_cast<double>(pref_satisfied) / pref_total}});
}

Result Service::complete_group(int group_id) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    const SmResult r = sm_.complete_group(group_id, now);
    if (r.code != 0) return error_result(r.code, r.message);
    return ok_result({{"group_id", group_id}, {"status", "completed"}});
}

// ---- 虚拟乘客 ----

Result Service::virtual_generate(const json& body) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    int count = config_.stream_batch_size;
    if (body.contains("count")) {
        if (!body["count"].is_number_integer()) return error_result(40001, "count 必须为整数");
        count = body["count"].get<int>();
        if (count < 1 || count > 1000) return error_result(40001, "count 越界（1-1000）");
    }
    std::string date = epoch_ms_to_date(now);
    if (body.contains("date")) {
        if (!body["date"].is_string()) return error_result(40001, "date 必须为字符串");
        date = body["date"].get<std::string>();
        int year = 0, month = 0, day = 0;
        if (!parse_date(date, &year, &month, &day))
            return error_result(40001, "date 非法（YYYY-MM-DD）");
    }
    const std::vector<int> ids = generate_locked(count, date, now);
    return ok_result({{"generated", ids}});
}

Result Service::stream_start(const json& body) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    if (stream_running_) return error_result(40903, "虚拟流已在运行");
    FieldError err;
    int64_t interval = config_.stream_interval_ms;
    if (!read_int_config(body, "interval_ms", 1, 3600000, &interval, &err))
        return error_result(err);
    int64_t batch = config_.stream_batch_size;
    if (!read_int_config(body, "batch_size", 1, 1000, &batch, &err)) return error_result(err);
    config_.stream_interval_ms = interval;
    config_.stream_batch_size = static_cast<int>(batch);

    stream_running_ = true;
    events_.append("stream_started", json::object(), now);
    // 首批立即生成（演示即时可见，也满足 AC-2.3「启动后 10s 内出现 created」）
    generate_locked(config_.stream_batch_size, epoch_ms_to_date(now), now);
    next_stream_ms_ = now + std::max<int64_t>(config_.stream_interval_ms, 1);
    return ok_result({{"running", true}});
}

Result Service::stream_stop() {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    const bool was_running = stream_running_;
    stream_running_ = false;
    next_stream_ms_ = 0;
    if (was_running) events_.append("stream_stopped", json::object(), now);
    return ok_result({{"running", false}});
}

// ---- 配置与重置 ----

Result Service::get_config() {
    std::lock_guard<std::mutex> lock(mu_);
    tick_locked(now_ms());
    return ok_result(config_to_json());
}

Result Service::set_config(const json& body) {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    tick_locked(now);
    Config next = config_;
    FieldError err;
    int64_t batch_size = config_.stream_batch_size;
    if (!read_int_config(body, "proposal_ttl_ms", 0, 3600000, &next.proposal_ttl_ms, &err) ||
        !read_int_config(body, "virtual_agree_delay_ms", 0, 3600000, &next.virtual_agree_delay_ms,
                         &err) ||
        !read_int_config(body, "auto_match_interval_ms", 0, 3600000, &next.auto_match_interval_ms,
                         &err) ||
        !read_int_config(body, "stream_interval_ms", 1, 3600000, &next.stream_interval_ms, &err) ||
        !read_int_config(body, "stream_batch_size", 1, 1000, &batch_size, &err)) {
        return error_result(err);
    }
    next.stream_batch_size = static_cast<int>(batch_size);
    if (body.contains("virtual_agree_prob")) {
        if (!body["virtual_agree_prob"].is_number()) {
            return error_result(40001, "virtual_agree_prob 必须为数字");
        }
        const double v = body["virtual_agree_prob"].get<double>();
        if (v < 0.0 || v > 1.0) return error_result(40001, "virtual_agree_prob 越界（0-1）");
        next.virtual_agree_prob = v;
    }

    const bool was_on = config_.auto_match_interval_ms > 0;
    const bool now_on = next.auto_match_interval_ms > 0;
    config_ = next;
    apply_engine_config_locked();
    if (now_on)
        next_auto_match_ms_ = now + config_.auto_match_interval_ms;  // 周期变化重新排程
    else
        next_auto_match_ms_ = 0;
    if (!was_on && now_on) events_.append("auto_match_started", json::object(), now);
    if (was_on && !now_on) events_.append("auto_match_stopped", json::object(), now);
    events_.append("config_changed", {{"config", config_to_json()}}, now);
    return ok_result(config_to_json());
}

Result Service::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    const int64_t now = now_ms();
    pool_.clear();
    sm_.reset();
    engine_.reset();
    events_.clear();
    config_ = Config{};
    rng_ = std::mt19937(seed_);  // 随机序列复位：演示可复现
    apply_engine_config_locked();
    stream_running_ = false;
    next_stream_ms_ = 0;
    next_auto_match_ms_ = now + config_.auto_match_interval_ms;
    events_.append("reset", json::object(), now);
    return ok_result(json::object());
}

// ---- 内部 ----

void Service::tick_locked(int64_t now) {
    sm_.tick(now);  // 提案超时 + 虚拟乘客自动同意 + 团到点自动完成
    if (config_.auto_match_interval_ms > 0 && now >= next_auto_match_ms_) {
        run_match_locked(now);
        next_auto_match_ms_ = now + config_.auto_match_interval_ms;
    }
    if (stream_running_ && now >= next_stream_ms_) {
        generate_locked(config_.stream_batch_size, epoch_ms_to_date(now), now);
        next_stream_ms_ = now + std::max<int64_t>(config_.stream_interval_ms, 1);
    }
}

std::vector<Proposal> Service::run_match_locked(int64_t now) {
    std::vector<Proposal> proposals = engine_.run_once(now);  // 引擎内部发 proposed 事件
    sm_.register_proposals(proposals);
    return proposals;
}


std::vector<int> Service::generate_locked(int count, const std::string& date, int64_t now) {
    const std::vector<int> ids = generator_.generate(count, date, rng_);
    for (int id : ids) events_.append("created", {{"passenger_id", id}}, now);
    events_.append("virtual_generated",
                   {{"count", static_cast<int>(ids.size())}, {"passenger_ids", ids}}, now);
    return ids;
}

json Service::config_to_json() const {
    return json{{"proposal_ttl_ms", config_.proposal_ttl_ms},
                {"virtual_agree_prob", config_.virtual_agree_prob},
                {"virtual_agree_delay_ms", config_.virtual_agree_delay_ms},
                {"auto_match_interval_ms", config_.auto_match_interval_ms},
                {"stream_interval_ms", config_.stream_interval_ms},
                {"stream_batch_size", config_.stream_batch_size}};
}

void Service::apply_engine_config_locked() {
    EngineConfig engine_config;
    engine_config.proposal_ttl_ms = config_.proposal_ttl_ms;
    engine_.set_config(engine_config);
}

}  // namespace merging
