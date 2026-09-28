# 拼车匹配工具 · 测试报告

> 逐阶段累积验收证据。约定：每条 AC 记录「验收标准 / 命令 / 预期 / 实测 / 证据」。

## G0（P0 环境脚手架）验收记录

| AC | 内容 | 结果 | 证据 |
| --- | --- | --- | --- |
| G0-1 | cmake 构建退出码 0 | 通过 | `cmake -S . -B build && cmake --build build` → exit 0，产物 `build/backend/merging_server` |
| G0-2 | `/api/health` 返回统一信封 code=0 | 通过 | `curl http://127.0.0.1:8080/api/health` → `{"code":0,"data":{"uptime_ms":...},"message":"ok"}` |
| G0-3 | 前端经 Vite 代理访问后端 | 通过 | `curl http://127.0.0.1:5173/api/health` → `{"code":0,...}`；页面 `<title>机场拼车匹配工具</title>` |
| G0-4 | clang-format 可用且 format_check 通过 | 通过 | `scripts/format_check.sh` → "格式检查通过 (3 个文件)"（clang-format 23.1.1，pip 安装于 ~/.local/bin） |
| G0-5 | a/p0-* 与 b/p0-* 分支经 PR 流程合入 main | 通过（本地模拟） | 合并提交 3d758d1（a/p0-scaffold）、a34143b（b/p0-frontend）；远端 push 待用户授权 |

## G1（P1 静态视图 + 契约冻结）验收记录

| AC | 内容 | 结果 | 证据 |
| --- | --- | --- | --- |
| G1-1 | mock 字段名与 contract.md §1 完全一致 | 通过 | `frontend/src/mock.js` 逐字段核对；11 条乘客覆盖全部 5 种 status、3 种 gender_preference、is_virtual；同车时间窗重叠（max(start) ≤ min(end)）、depart_min = max(start)、每团 ≤4 人、偏好不满足不成团，均按契约规则手工校验 |
| G1-2 | mock 数据渲染正确 | 通过 | `npm run dev` 后 Edge 无头浏览器 dump-dom：匹配池 9 列表头、11 行数据、状态标签计数与 mock 完全一致（waiting×3 / proposed×2 / grouped×3 / completed×2 / cancelled×1 / virtual×2）、时间窗转换全部正确、22 个按钮中仅 proposed 行 4 个可用；成团视图经 Vite SSR 渲染验证：团 #1/#2 两张卡片、总人数 computed 各 4 人、完成按钮 1 可用 1 禁用；截图见 [docs/images/G1_pool_view.png](images/G1_pool_view.png) |
| G1-3 | `npm run build` 退出码 0 | 通过 | `vite v8.3.1 building client environment for production... ✓ 26 modules transformed. ✓ built in 475ms`，exit 0 |
| G1-4 | A/B 共同确认契约字段，A 的 P1 PR 将契约置 v1.0 / FROZEN | 待 A | — |

> G1 备注（B 侧契约核对发现，冻结前需与 A 双人确认后改 contract.md，建议开 issue @A）：
> 1. FR-10 `/api/match/pool` 仅返回 waiting，但 PoolView 需展示 proposed 乘客（同意/拒绝按钮的落点，见手册 §3），数据来源缺口；
> 2. 无「按 id 查询乘客」的 GET 端点，`/api/match/groups` 响应仅 member_ids，成团卡片无法展示成员明细（性别/人数）；mock 已按明细渲染，接真实接口前需定方案；
> 3. group 对象无 status 字段，已完成团的区分方式未约定（完成按钮禁用态依赖它）。

## G2~G4 验收记录

（待各阶段完成后追加）
