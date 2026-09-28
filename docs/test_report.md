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
| G1-1 | mock 字段名与 contract.md §1 完全一致 | 通过 | `frontend/src/mock.js` 逐字段核对；8 条乘客覆盖 3 种非终态 status（waiting/proposed/grouped）、3 种 gender_preference、is_virtual；同车时间窗重叠（max(start) ≤ min(end)）、depart_min = max(start)、每团 ≤4 人、偏好不满足不成团，均按契约规则手工校验 |
| G1-2 | mock 数据渲染正确 | 通过 | `npm run dev` 后 Edge 无头浏览器 dump-dom：匹配池 9 列表头、8 行数据、状态标签计数与 mock 完全一致（waiting×3 / proposed×2 / grouped×3 / virtual×1）、时间窗转换全部正确、16 个按钮中仅 proposed 行 4 个可用；成团视图经 Vite SSR 渲染验证：团 #1 卡片、总人数 computed 为 4 人；截图见 [docs/images/G1_pool_view.png](images/G1_pool_view.png) |
| G1-3 | `npm run build` 退出码 0 | 通过 | `vite v8.3.1 building client environment for production... ✓ 26 modules transformed. ✓ built in 475ms`，exit 0 |
| G1-4 | A/B 共同确认契约字段后冻结 v1.0 | 进行中 | 契约调整（FR-10 放宽、FR-11 明确）已由 B 写入 contract.md/contract.yaml，待 A 校对后置 FROZEN |

> G1 备注（契约调整记录）：mock 先行阶段暴露 3 条契约缺口，经双人裁定已在 v1.0 草案中闭环（待 A 校对后冻结）：
> 1. **FR-10 数据源缺口**（pool 仅返回 waiting，池视图无法展示 proposed 乘客）→ 已放宽：pool 返回非终态乘客 waiting/proposed/grouped；
> 2. **无「按 id 查乘客」端点**（groups 仅 member_ids，卡片无成员明细）→ 由第 1 条解决：前端用 pool 数据 join（装配逻辑见 GroupsView）；
> 3. **group 无 status**（已完成/已解散团的区分未约定）→ 已明确：FR-11 仅返回进行中的团，completed/dissolved 即时移除，无需状态字段。

## G2~G4 验收记录

（待各阶段完成后追加）
