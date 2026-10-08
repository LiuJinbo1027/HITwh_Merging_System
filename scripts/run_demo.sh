#!/usr/bin/env bash
# P3 一键演示（Gate G3 / AC-3.2）：
#   自起后端(8080) + 前端 vite(5173) → 浏览器直达总控台 → 开自动匹配 + 启动虚拟流
#   → 轮询直到成团（轮询同时驱动后端懒 tick）→ 保持运行，Ctrl+C 一键清理。
# 用法: scripts/run_demo.sh [后端端口] [前端端口]
# 兼容 macOS 自带 bash 3.2（不用 mapfile / 关联数组 / ${var,,}）。
set -uo pipefail
cd "$(dirname "$0")/.."

BE_PORT="${1:-8080}"
FE_PORT="${2:-5173}"
BIN="./build/backend/merging_server"
BE_LOG="${TMPDIR:-/tmp}/run_demo_backend.$$.log"
FE_LOG="${TMPDIR:-/tmp}/run_demo_vite.$$.log"
API="http://127.0.0.1:${BE_PORT}/api"
URL="http://localhost:${FE_PORT}/#control"

die() {
    echo "错误: $1" >&2
    exit 2
}
command -v jq >/dev/null 2>&1 || die "需要 jq（macOS: brew install jq；Ubuntu: apt install jq）"
command -v curl >/dev/null 2>&1 || die "需要 curl"
[[ -x "${BIN}" ]] || die "找不到 ${BIN} —— 先构建：cmake -S . -B build && cmake --build build -j4"
if curl -s -o /dev/null -m 2 "${API}/health"; then
    die "端口 ${BE_PORT} 已有服务（先停掉，或换端口：scripts/run_demo.sh 8081 5174）"
fi
if curl -s -o /dev/null -m 2 "http://localhost:${FE_PORT}/"; then
    die "端口 ${FE_PORT} 已有服务（先停掉，或换端口：scripts/run_demo.sh 8080 5174）"
fi

BACKEND_PID=""
VITE_PID=""
cleanup() {
    [[ -n "${BACKEND_PID}" ]] && kill "${BACKEND_PID}" 2>/dev/null
    [[ -n "${VITE_PID}" ]] && kill "${VITE_PID}" 2>/dev/null
    # npm 的子进程（真正的 vite/node）可能幸存：按端口兜底清理。
    # 端口在启动前已确认空闲，此处清理的必然是本脚本起的进程。
    kill "$(lsof -ti:"${BE_PORT}" 2>/dev/null)" 2>/dev/null
    kill "$(lsof -ti:"${FE_PORT}" 2>/dev/null)" 2>/dev/null
    wait 2>/dev/null
    rm -f "${BE_LOG}" "${FE_LOG}"
    echo "已清理：后端/前端进程已停止。"
}
trap cleanup EXIT INT TERM

echo "== 1/4 启动后端（端口 ${BE_PORT}） =="
"${BIN}" "${BE_PORT}" >"${BE_LOG}" 2>&1 &
BACKEND_PID=$!
READY=0
for _ in $(seq 1 50); do
    curl -s -o /dev/null -m 1 "${API}/health" && READY=1 && break
    sleep 0.2
done
[[ "$READY" == "1" ]] || die "后端 10 秒内未就绪（日志：${BE_LOG}）"
echo "后端就绪（pid ${BACKEND_PID}）"

echo "== 2/4 启动前端 vite（端口 ${FE_PORT}，日志 ${FE_LOG}） =="
# 代理目标跟随后端端口（vite.config.js 读 VITE_PROXY_TARGET，默认 8080）
(cd frontend && VITE_PROXY_TARGET="http://127.0.0.1:${BE_PORT}" \
    npm run dev -- --port "${FE_PORT}" >"${FE_LOG}" 2>&1) &
VITE_PID=$!
READY=0
for _ in $(seq 1 50); do
    curl -s -o /dev/null -m 1 "http://localhost:${FE_PORT}/" && READY=1 && break
    sleep 0.2
done
[[ "$READY" == "1" ]] || die "前端 10 秒内未就绪（日志：${FE_LOG}）"

echo "== 3/4 打开浏览器（${URL}）+ 开自动匹配 + 启动虚拟流 =="
if [[ "${RUN_DEMO_NO_BROWSER:-0}" == "1" ]]; then
    echo "（RUN_DEMO_NO_BROWSER=1，跳过开浏览器）"
elif [[ "$(uname -s)" == "Darwin" ]]; then
    open "${URL}"
else
    xdg-open "${URL}" >/dev/null 2>&1 || echo "（无法自动开浏览器，请手动打开 ${URL}）"
fi
curl -s -X PUT "${API}/config" -H 'Content-Type: application/json' \
    -d '{"auto_match_interval_ms":2000}' >/dev/null
echo "自动匹配：每 2s 一轮"
curl -s -X POST "${API}/virtual/stream/start" -H 'Content-Type: application/json' \
    -d '{"interval_ms":1500,"batch_size":3}' >/dev/null
echo "虚拟流：每 1.5s 一批 3 人（首批已生成；虚拟乘客在提案中会自动同意）"

echo "== 4/4 等待成团（轮询驱动后端 tick） =="
# 注意：不能用 GROUPS 作变量名——bash 内置只读数组（用户组 ID），赋值静默失败、
# 读出来恒 ≥1 导致循环第一轮就退出（bash 3.2 实测踩坑）。
GROUP_COUNT=0
for _ in $(seq 1 60); do
    sleep 1
    GROUP_COUNT=$(curl -s "${API}/stats" | jq '.data.group_count')
    [[ "${GROUP_COUNT}" -ge 1 ]] && break
done
echo
curl -s "${API}/stats" | jq -r '"池大小 \(.data.pool_size) · 进行中团 \(.data.group_count) · 平均每车 \(.data.avg_group_size) 人 · 偏好满足率 \(.data.gender_pref_satisfied_ratio * 100 | floor)%"'
curl -s "${API}/match/groups" | jq -r '.data.groups[] | "团#\(.group_id) 出发 \(.depart_min)min 成员 [\(.member_ids | join(","))]"'
echo
echo "演示进行中：浏览器四视图实时刷新（池/团/事件）。Ctrl+C 结束并清理。"
# 用可中断的 sleep 循环保活（bash 3.2 在 wait 中收到 SIGINT 不会及时执行 trap）
while :; do
    sleep 1
done
