#!/usr/bin/env bash
# P2 端到端 smoke（Gate G2 / AC-2.1 + AC-2.3）：
#   自起 ./build/backend/merging_server → 17 个端点逐一 curl + jq 断言
#   → 真实时间虚拟流（轮询 /api/events 驱动 tick，10s 内出现 ≥2 批 created）→ 汇总。
# 用法: scripts/smoke_api.sh [端口]    （默认 8099，CI 无参数直接跑）
# 兼容 macOS 自带 bash 3.2（不用 mapfile / 关联数组 / ${var,,}）+ GNU bash 4/5。
# 覆盖端点：/api/health、/api/passengers、/api/passengers/{id}(PUT/DELETE)、agree、reject、
#   /api/match/trigger、optimize、pool、groups、/api/events、/api/stats、
#   /api/groups/{id}/complete、/api/virtual/generate、virtual/stream/start、stream/stop、
#   /api/config(GET/PUT)、/api/reset —— 共 17 个路径 / 19 条路由。
set -uo pipefail

PORT="${1:-8099}"
cd "$(dirname "$0")/.."

BIN="./build/backend/merging_server"
LOG="${TMPDIR:-/tmp}/merging_smoke.$$.log"
API="http://127.0.0.1:${PORT}/api"
DAY="2030-06-01"  # 远期日期：保证「到点自动完成」不会在 smoke 期间触发（确定性）

PASS=0
FAIL=0
REPLY=""
HTTP_CODE=""

section() { echo; echo "== $1 =="; }
ok() { PASS=$((PASS + 1)); printf '  ok   %s\n' "$1"; }
bad() { FAIL=$((FAIL + 1)); printf '  FAIL %s\n' "$1"; }
check_eq() { # check_eq <描述> <实际> <期望>
    if [[ "$2" == "$3" ]]; then ok "$1"; else bad "$1（期望 [$3]，实际 [$2]）"; fi
}
check_ge() { # check_ge <描述> <实际> <下限>
    if [[ "$2" -ge "$3" ]]; then ok "$1"; else bad "$1（期望 >= $3，实际 [$2]）"; fi
}
jget() { printf '%s' "$REPLY" | jq -r "$1"; }
die() {
    echo "错误: $1" >&2
    exit 2
}

# req <METHOD> <PATH> [JSON_BODY] → REPLY=响应体, HTTP_CODE=状态码。
# 注意：POST/PUT/DELETE 一律带 body（httplib 无 Content-Length 时直接 400，不进业务层）。
req() {
    local method="$1" path="$2" body="${3-}" out
    if [[ -n "$body" ]]; then
        out=$(curl -sS -X "$method" "${API}${path}" -H 'Content-Type: application/json' -d "$body" \
            -w '\n%{http_code}') || out=""
    else
        out=$(curl -sS -X "$method" "${API}${path}" -w '\n%{http_code}') || out=""
    fi
    if [[ -z "$out" ]]; then
        bad "curl ${method} ${path} 无响应（连接失败）"
        REPLY=""
        HTTP_CODE="000"
        return 0
    fi
    HTTP_CODE=$(printf '%s' "$out" | tail -n 1)
    REPLY=$(printf '%s' "$out" | sed '$d')
}

expect_ok() { check_eq "$1: code=0" "$(jget '.code')" "0"; }
expect_err() { # expect_err <描述> <错误码>：失败信封（code / HTTP 400 / data=null）
    check_eq "$1: code=$2" "$(jget '.code')" "$2"
    check_eq "$1: HTTP 400" "$HTTP_CODE" "400"
    check_eq "$1: data=null" "$(jget '.data == null')" "true"
}

# 池视图辅助：按 id 取字段（池只含非终态乘客）
pax_field() { printf '%s' "$REPLY" | jq -r --argjson id "$1" --arg f "$2" '.data.passengers[] | select(.passenger_id == $id) | .[$f]'; }
ev_count() { printf '%s' "$REPLY" | jq --arg t "$1" '[.data.events[] | select(.event_type == $t)] | length'; }

# ---- 前置检查 ----
command -v jq >/dev/null 2>&1 || die "需要 jq（macOS: brew install jq；Ubuntu: apt install jq）"
[[ -x "$BIN" ]] || die "找不到 $BIN —— 先构建：cmake -S . -B build && cmake --build build -j4"
if curl -s -o /dev/null -m 2 "http://127.0.0.1:${PORT}/api/health"; then
    die "端口 ${PORT} 已有服务在跑（换端口：scripts/smoke_api.sh 8100）"
fi

"$BIN" "$PORT" >"$LOG" 2>&1 &
SERVER_PID=$!
cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -f "$LOG"
}
trap cleanup EXIT INT TERM

section "启动服务（端口 ${PORT}，pid ${SERVER_PID}）"
READY=0
# 启动期连接被拒是预期行为：等待用静默 curl，不计入断言
for _ in $(seq 1 50); do
    if curl -s -o /dev/null -m 1 "http://127.0.0.1:${PORT}/api/health"; then
        READY=1
        break
    fi
    sleep 0.2
done
if [[ "$READY" != "1" ]]; then
    echo "--- 服务日志 ---" >&2
    cat "$LOG" >&2
    die "服务 10 秒内未就绪"
fi
req GET /health
expect_ok "GET /api/health"
check_eq "health uptime_ms 为整数" "$(jget '.data.uptime_ms | type')" "number"

# ---- 配置（端点 16）：先关自动匹配保证确定性；校验默认值 ----
section "配置（/api/config GET/PUT）"
req PUT /config '{"auto_match_interval_ms":0}'
expect_ok "PUT /api/config 关闭自动匹配"
check_eq "auto_match_interval_ms 即改即生效" "$(jget '.data.auto_match_interval_ms')" "0"
req GET /config
expect_ok "GET /api/config"
check_eq "默认 proposal_ttl_ms" "$(jget '.data.proposal_ttl_ms')" "60000"
check_eq "默认 virtual_agree_prob" "$(jget '.data.virtual_agree_prob')" "0.9"
check_eq "默认 stream_batch_size" "$(jget '.data.stream_batch_size')" "5"
req PUT /config '{"proposal_ttl_ms":-1}'
expect_err "非法配置（TTL 负数）" 40001

# ---- 乘客（端点 1）：注册 + 参数错误 ----
section "乘客注册（/api/passengers）"
req POST /passengers "{\"party_size\":2,\"gender\":\"female\",\"gender_preference\":\"female_only\",\"date\":\"${DAY}\",\"start_min\":500,\"end_min\":600}"
expect_ok "注册 P1"
P1=$(jget '.data.passenger_id')
check_eq "P1 初始状态" "$(jget '.data.status')" "waiting"
req POST /passengers "{\"party_size\":2,\"gender\":\"female\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":510,\"end_min\":600}"
expect_ok "注册 P2"
P2=$(jget '.data.passenger_id')
req POST /passengers "{\"party_size\":1,\"gender\":\"male\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":900,\"end_min\":1000}"
expect_ok "注册 P3（独立窗口）"
P3=$(jget '.data.passenger_id')

req POST /passengers "{\"party_size\":5,\"gender\":\"male\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":500,\"end_min\":600}"
expect_err "party_size=5" 40001
req POST /passengers "{\"party_size\":1,\"gender\":\"male\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":700,\"end_min\":600}"
expect_err "start_min>end_min" 40902
req POST /passengers "{\"party_size\":1,\"gender\":\"male\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":500}"
expect_err "缺少 end_min" 40001

# ---- 匹配 + 同意（端点 4/5/7/9/10） ----
section "触发匹配 → 全员同意成团（/api/match/trigger、/agree、/groups）"
req POST /match/trigger '{}'
expect_ok "POST /api/match/trigger"
check_eq "提案数=1" "$(jget '.data.proposals | length')" "1"
check_eq "提案成员=P1,P2" "$(jget ".data.proposals[0].member_ids | sort | join(\",\")")" "${P1},${P2}"
check_eq "depart_min=max(start)=510" "$(jget '.data.proposals[0].depart_min')" "510"

req POST "/passengers/${P1}/agree" '{}'
expect_ok "P1 同意"
check_eq "未全员同意 → pending" "$(jget '.data.proposal_state')" "pending"
req POST "/passengers/${P2}/agree" '{}'
expect_ok "P2 同意"
check_eq "全员同意 → grouped" "$(jget '.data.proposal_state')" "grouped"

req POST "/passengers/${P3}/agree" '{}'
expect_err "waiting 乘客同意（P3）" 40901
req POST "/passengers/99999/agree" '{}'
expect_err "未知乘客同意" 40401
req POST "/passengers/${P3}/reject" '{}'
expect_err "waiting 乘客拒绝（P3）" 40901

req GET /match/groups
expect_ok "GET /api/match/groups"
check_eq "进行中团数=1" "$(jget '.data.groups | length')" "1"
GID=$(jget '.data.groups[0].group_id')
check_eq "团成员数=2" "$(jget '.data.groups[0].member_ids | length')" "2"

req GET /match/pool
expect_ok "GET /api/match/pool"
check_eq "P1 状态 grouped" "$(pax_field "$P1" status)" "grouped"
check_eq "P2 状态 grouped" "$(pax_field "$P2" status)" "grouped"
check_eq "P3 状态 waiting" "$(pax_field "$P3" status)" "waiting"

# ---- 统计（端点 11） ----
section "统计（/api/stats）"
req GET /stats
expect_ok "GET /api/stats"
check_eq "pool_size=3（非终态）" "$(jget '.data.pool_size')" "3"
check_eq "group_count=1" "$(jget '.data.group_count')" "1"
check_eq "avg_group_size=4（2+2 人）" "$(jget '.data.avg_group_size * 100 | round')" "400"
check_eq "female_ratio=2/3" "$(jget '.data.female_ratio * 3 | round')" "2"
check_eq "偏好满足率=1.0（P1 限女、同车皆女）" "$(jget '.data.gender_pref_satisfied_ratio * 100 | round')" "100"

# ---- 完成（端点 12） / 修改（端点 2） / 取消（端点 3） ----
section "完成 / 修改 / 取消（/api/groups/{id}/complete、PUT、DELETE /api/passengers/{id}）"
req POST "/groups/${GID}/complete" '{}'
expect_ok "完成团"
check_eq "完成状态" "$(jget '.data.status')" "completed"
req POST "/groups/${GID}/complete" '{}'
expect_err "重复完成" 40401
req GET /match/pool
check_eq "完成后 P1 移出池视图" "$(pax_field "$P1" status)" ""
check_eq "池中仅剩 P3" "$(jget '.data.passengers | length')" "1"

req PUT "/passengers/${P3}" '{"start_min":950}'
expect_ok "PUT 改 P3 时间窗"
req GET /match/pool
check_eq "新时间窗生效" "$(pax_field "$P3" start_min)" "950"
req PUT "/passengers/${P3}" '{"start_min":1100}'
expect_err "PUT 造成 start>end" 40902
req PUT "/passengers/99999" '{"start_min":500}'
expect_err "PUT 未知乘客" 40401

req DELETE "/passengers/${P3}" '{}'
expect_ok "取消 P3"
check_eq "取消状态" "$(jget '.data.status')" "cancelled"
req DELETE "/passengers/${P3}" '{}'
expect_err "重复取消" 40901
req GET /match/pool
check_eq "取消后池空" "$(jget '.data.passengers | length')" "0"

# ---- 事件（端点 11） ----
section "事件增量轮询（/api/events）"
req GET "/events?since_id=0&limit=1000"
expect_ok "GET /api/events"
check_ge "事件条数" "$(jget '.data.events | length')" 10
NEXT=$(jget '.data.next_since_id')
check_eq "next_since_id=最后一条 id" "$(jget '.data.events[-1].event_id')" "$NEXT"
for t in created updated cancelled proposed agreed group_formed completed config_changed; do
    check_ge "事件类型 ${t} 出现" "$(ev_count "$t")" 1
done
req GET "/events?since_id=${NEXT}&limit=1000"
check_eq "增量无新事件时为空" "$(jget '.data.events | length')" "0"
check_eq "next_since_id 回显" "$(jget '.data.next_since_id')" "$NEXT"

# ---- 优化匹配（端点 8，P2 等价贪心） ----
section "优化匹配（/api/match/optimize）"
req POST /passengers "{\"party_size\":1,\"gender\":\"male\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":400,\"end_min\":500}"
expect_ok "注册 P4"
P4=$(jget '.data.passenger_id')
req POST /passengers "{\"party_size\":1,\"gender\":\"female\",\"gender_preference\":\"none\",\"date\":\"${DAY}\",\"start_min\":410,\"end_min\":500}"
expect_ok "注册 P5"
P5=$(jget '.data.passenger_id')
req POST /match/optimize '{}'
expect_ok "POST /api/match/optimize"
check_eq "optimize 提案数=1" "$(jget '.data.proposals | length')" "1"
check_eq "optimize 提案成员" "$(jget ".data.proposals[0].member_ids | sort | join(\",\")")" "${P4},${P5}"
req POST "/passengers/${P4}/agree" '{}'
expect_ok "optimize 提案可同意（已登记）"
req POST "/passengers/${P5}/agree" '{}'
check_eq "optimize 提案成团" "$(jget '.data.proposal_state')" "grouped"

# ---- 虚拟乘客：批量生成（端点 13） ----
section "虚拟乘客生成（/api/virtual/generate）"
req POST /virtual/generate "{\"count\":3,\"date\":\"${DAY}\"}"
expect_ok "批量生成 3 名"
check_eq "generated 条数" "$(jget '.data.generated | length')" "3"
req POST /virtual/generate '{"count":0}'
expect_err "count=0" 40001
req POST /virtual/generate '{"count":3,"date":"2030-13-01"}'
expect_err "日期非法" 40001

# ---- 虚拟流 + AC-2.3 真实时间（端点 14/15） ----
section "虚拟流（/api/virtual/stream/start|stop）+ AC-2.3 真实时间断言"
req GET /events
SINCE=$(jget '.data.next_since_id')
req POST /virtual/stream/start '{"interval_ms":1000,"batch_size":3}'
expect_ok "启动虚拟流"
check_eq "running=true" "$(jget '.data.running')" "true"
req POST /virtual/stream/start '{"interval_ms":1000,"batch_size":3}'
expect_err "重复启动虚拟流" 40903

BATCH=3
TRIES=0
CREATED=0
T0=$(date +%s)
while [[ "$TRIES" -lt 33 ]]; do
    req GET "/events?since_id=${SINCE}&limit=1000"
    CREATED=$(ev_count created)
    # 首批已在 start 时同步生成；这里等第二批证明「间隔到期 → 流持续生成」由轮询驱动
    if [[ "$CREATED" -ge $((BATCH * 2)) ]]; then
        break
    fi
    sleep 0.3
    TRIES=$((TRIES + 1))
done
ELAPSED=$(( $(date +%s) - T0 ))
check_ge "AC-2.3 流启动后 created >= batch_size" "$CREATED" "$BATCH"
check_ge "AC-2.3 第二批到点生成（>= 2×batch）" "$CREATED" "$((BATCH * 2))"
check_ge "AC-2.3 10 秒内完成（实际 ${ELAPSED}s）" "10" "$((ELAPSED + 1))"
check_eq "stream_started 事件" "$(ev_count stream_started)" "1"

req POST /virtual/stream/stop '{}'
expect_ok "停止虚拟流"
check_eq "running=false" "$(jget '.data.running')" "false"
req GET "/events?since_id=${SINCE}&limit=1000"
STOPPED_AT=$(ev_count created)
sleep 2
req GET "/events?since_id=${SINCE}&limit=1000"
check_eq "停止后不再生成" "$(ev_count created)" "$STOPPED_AT"

# ---- 重置（端点 17） ----
section "重置（/api/reset）"
req POST /reset '{}'
expect_ok "POST /api/reset"
req GET /match/pool
check_eq "reset 后池空" "$(jget '.data.passengers | length')" "0"
req GET /match/groups
check_eq "reset 后无团" "$(jget '.data.groups | length')" "0"
req GET /config
check_eq "reset 后配置回默认" "$(jget '.data.auto_match_interval_ms')" "5000"
req GET /events?since_id=0
check_eq "reset 后仅 reset 事件" "$(jget '.data.events | length')" "1"
check_eq "reset 事件类型" "$(jget '.data.events[0].event_type')" "reset"
check_ge "事件 id 不倒退（保持单调）" "$(jget '.data.events[0].event_id')" "$((NEXT + 1))"

# ---- 汇总 ----
echo
echo "========================= smoke 汇总 ========================="
echo "PASS: ${PASS}    FAIL: ${FAIL}"
if [[ "$FAIL" -gt 0 ]]; then
    echo "结果: FAIL"
    exit 1
fi
echo "结果: PASS（17 端点全部可用 + AC-2.3 真实时间虚拟流）"
