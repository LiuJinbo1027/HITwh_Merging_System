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
| G1-4 | A/B 共同确认契约字段后冻结 v1.0 | 通过 | A 的 P1 PR 合入 main（合并提交 5430eee a/p1-engine）：版本头 1.0、status FROZEN；P2 仅补充语义澄清（无字段增删） |

> G1 备注（契约调整记录）：mock 先行阶段暴露 3 条契约缺口，经双人裁定已在 v1.0 草案中闭环（待 A 校对后冻结）：
> 1. **FR-10 数据源缺口**（pool 仅返回 waiting，池视图无法展示 proposed 乘客）→ 已放宽：pool 返回非终态乘客 waiting/proposed/grouped；
> 2. **无「按 id 查乘客」端点**（groups 仅 member_ids，卡片无成员明细）→ 由第 1 条解决：前端用 pool 数据 join（装配逻辑见 GroupsView）；
> 3. **group 无 status**（已完成/已解散团的区分未约定）→ 已明确：FR-11 仅返回进行中的团，completed/dissolved 即时移除，无需状态字段。

## G2（P2 HTTP 接口 + 状态机 + 虚拟乘客 + 前端表单）验收记录

> 本阶段 A 交付 17 端点 + 状态机/虚拟流/事件流后端；B 交付 smoke 脚本（合写）、`docs/state_machine.md`（A 校对）、`components/{PassengerForm,VirtualControl}.vue` 接真实接口、PoolView 接 `/api/match/pool` 实时轮询。

| AC | 内容 | 结果 | 证据 |
| --- | --- | --- | --- |
| AC-2.1 | smoke_api.sh 对 17 个端点逐一断言（成功路径 code=0 / 预期错误码） | 通过 | `scripts/smoke_api.sh 8099` → `PASS: 122  FAIL: 0`，exit 0（自起服务 8099，17 路径 / 19 条路由全覆盖） |
| AC-2.2 | 状态机全路径单测（全部假时钟） | 通过 | `ctest --test-dir build --output-on-failure` → `100% tests passed, 0 tests failed out of 6`（test_time_bucket / test_pool / test_engine / test_state_machine / test_virtual_source / test_service） |
| AC-2.3 | 虚拟流启动后 10 秒内出现 ≥ batch_size 个 created | 通过 | smoke 内真实时间断言：`AC-2.3 流启动后 created >= batch_size`、`第二批到点生成（>= 2×batch）`、`10 秒内完成（实际 1s）` 均 ok |
| AC-2.4 | 错误码：waiting 乘客 agree→40901、未知 id→40401、start>end→40902、重复 stream/start→40903、party_size=5→40001 | 通过 | smoke 内 expect_err 断言全部 ok（错误信封 code / HTTP 400 / data=null 三要素） |
| B-P2-1 | docs/state_machine.md 与实现一致 | 通过（待 A 校对） | 逐条对照 `backend/src/state_machine.cpp` 写出：5 态集合、14 条转移表（T1-T14 带代码位置）、tick 三段自动化（超时/虚拟自动同意/到点完成）、事件流对照、错误码对照；A 校对后随 PR 合入 |
| B-P2-2 | 表单提交后池视图出现新乘客 | 通过 | 后端 8080 + `npm run dev` 后经 Vite 代理模拟表单同款请求：POST /api/passengers → `{"passenger_id":1,"status":"waiting"}`；GET /api/match/pool 立即含该乘客（9 字段全）；POST /api/virtual/generate {count:3} → `[2,3,4]`，池共 4 人。Edge 无头浏览器 dump-dom 实测：池视图轮询渲染出 #1「等待中」+ 3 个「虚拟」标签（无「加载中/未连接」）；截图见 [docs/images/G2_pool_real.png](images/G2_pool_real.png) |
| B-P2-3 | `npm run build` 退出码 0 | 通过 | `vite v8.3.1 building client environment for production... ✓ built in 86ms`，exit 0；ControlView SSR 渲染检查 9 项字段标签全部命中（party_size/gender/gender_preference/date/start_min/end_min/count） |
| B-P2-4 | 成团视图接真实接口（P3「四视图」项提前） | 通过 | P2 联调暴露：池视图已显示真实成团，成团页 mock 卡片与真实数据冲突。GroupsView 改接 `api.groups()` + `api.pool()`（Promise.all 同刻快照，member_ids 展开成员明细），GroupCard「完成」按钮接通 `api.completeGroup`；`frontend/src/mock.js` 删除。`npm run build` exit 0；真实数据链路：virtual/generate 4 名 → 自动匹配 + 虚拟自动同意成团 → `/api/match/groups` 返回团并成功装配成员明细 |

> G2 备注：PoolView 同意/拒绝按钮、虚拟流开关、自动匹配开关按手册划入 P3（ControlView 完整化）；GroupsView 真实数据原属 P3「四视图」，因与 P2 池视图真实数据冲突提前到本阶段（见 B-P2-4）。

## G3（P3 四视图 + 事件流 + 一键演示）验收记录

> 本阶段 B 交付：EventsView（since_id 增量轮询）、ControlView 完整化（StatBar + 触发匹配/自动匹配开关/虚拟流启停/重置）、PoolView 同意/拒绝按钮、`scripts/run_demo.sh` 一键演示；联调中修复 2 个脚本/配置问题（见备注）。

| AC | 内容 | 结果 | 证据 |
| --- | --- | --- | --- |
| AC-3.1 | 手动流程：GUI 录入 4 名乘客 → 触发匹配 → 点同意 → 全部同意 → 成团卡片出现 | 通过 | 后端 8082 + vite 5174 联调，按 GUI 同款请求序列实测：4×POST /api/passengers（同窗 08:00-10:00）→ trigger → `{"proposals":[{"proposal_id":1,"member_ids":[1,2,3,4],"depart_min":480}]}` → 4×agree → `pending/pending/pending/grouped` → `/api/match/groups` 返回团#1（4 成员）；截图 [G3_pool.png](images/G3_pool.png)、[G3_groups.png](images/G3_groups.png) |
| AC-3.2 | 一键 run_demo：池/团/事件三视图实时刷新；console 无报错；无轮询死循环 | 通过 | `RUN_DEMO_NO_BROWSER=1 ./scripts/run_demo.sh 8083 5175` 端到端：起后端→起前端→开自动匹配(2s)+虚拟流(1.5s/批3人)→轮询至成团 → `池大小 9 · 进行中团 2 · 平均每车 2.5 人`，团#1/#2 成员明细正确；Ctrl+C（TERM）一键清理、无残留进程。四视图经 Edge 无头浏览器 dump-dom 验证渲染真实数据（#control：StatBar 五指标+控件；#pool：4×已成团+团#1；#groups：卡片共4人+完成按钮；#events：created×4→proposed→agreed×4→group_formed 完整链路）。轮询采用 setTimeout-after-completion 模式（api.js usePolling），请求完成后才排下一次，无并发堆积 |
| AC-3.3 | 后端未启动时页面显示「后端未连接」 | 通过 | 顶栏徽章变红「后端未连接」（App.vue health 轮询）；池/团/事件视图各自显示错误态「后端未连接或响应异常：…（每 2 秒自动重试）」，后端启动后 2s 内自动恢复（P2 联调时经代理指向空 8080 实测） |
| B-P3-1 | EventsView since_id 增量轮询 | 通过 | 本地 lastId 递增拉取，新事件前插（最新在前）、封顶 500 条；17 种 event_type 映射语义色；#events 实测渲染 created/agreed/proposed/group_formed 完整事件链 |
| B-P3-2 | `npm run build` 退出码 0 | 通过 | `vite v8.3.1 building client environment for production... ✓ built in 76ms`，exit 0 |

> G3 备注（联调修复）：
> 1. **Vite 代理端口写死**：vite.config.js 原固定代理 8080，换后端端口即失联 → 支持 `VITE_PROXY_TARGET` 环境变量覆盖（默认仍 8080），run_demo.sh 起前端时同步传入；
> 2. **run_demo.sh 两个 bash 3.2 兼容坑**：① `GROUPS` 是 bash 只读内置数组（用户组 ID），赋值静默失败导致等待循环首轮误判退出 → 改名 `GROUP_COUNT`；② bash 3.2 在 `wait` 中收到 SIGINT 不执行 trap → 保活改用可中断的 `while :; do sleep 1; done`；③ 所有变量展开紧邻中文标点处一律用 `${...}` 花括号定界（macOS bash 3.2 会把多字节标点首字节吞进变量名）；
> 3. 附带小改动：App.vue 支持 URL 哈希直达视图（`/#control` 等，run_demo 与验收截图用）。

## G4（P4 实验 + 四份文档）验收记录

（P4 已按用户要求暂封存于 `.p4_archive/`，验收通过后按其中《恢复说明》恢复实施）


