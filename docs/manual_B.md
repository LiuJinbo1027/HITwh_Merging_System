# 拼车匹配工具 · B 开发手册（前端、联调与文档）

> 你的角色：**前端与文档负责人**。管理台 GUI 是课程设计演示与答辩的门面，文档质量决定验收印象。

## 0. 职责与边界

你负责（前端与文档）：Vite/Vue 工程与四视图组件、`api.js`、smoke 与 run_demo 脚本、`docs/{taskbook,state_machine,test_report,final_report,experiments}.md`、contract.md 维护、实验数据。

队友 A 负责（后端与算法）：C++ 匹配引擎、HTTP 接口实现、后端单测、`docs/design.md` 理论章节。

**共同约定**：`docs/contract.md` 是接口唯一事实源——改动任何 `/api` 字段必须先改它、双人确认后同 PR 提交。

## 1. 环境准备（拿到手册后第一件事）

### 1.1 获取代码

- 仓库：https://github.com/LiuJinbo1027/HITwh_Merging_System
- 仓库主人先在 GitHub → Settings → Collaborators → Add people 把你加为协作者（否则无法推送）。
- 克隆：`git clone git@github.com:LiuJinbo1027/HITwh_Merging_System.git`（SSH，需先在 GitHub 添加你的 SSH key；或 HTTPS + 个人 token）。

### 1.2 安装工具（Linux）

- Node ≥ 18（推荐 20+）、npm。前端不需要其他全局工具。
- 后端也要能自己跑（交叉验收需要）：cmake ≥ 3.16、g++（C++17）、clang-format，安装方式同 A 手册 1.2。

### 1.3 跑起来

```bash
cd frontend && npm install
npm run dev                 # http://127.0.0.1:5173，/api 自动代理到后端 8080
```

注意：先启动后端（`cmake -S . -B build && cmake --build build && ./build/backend/merging_server`）再开页面，否则页面显示"后端未连接"。

## 2. 协作纪律（与 A 手册第 2 节一致，双方必须遵守）

1. **分支**：你只用 `b/<step>-<名>` 前缀（如 `b/p1-mock-views`）；A 用 `a/`。从最新 main 切出。
2. **提交信息**：`type(scope): summary`，如 `feat(views): 匹配池静态表格`、`docs(contract): 冻结 v1 字段核对`。
3. **PR 流程**：推送分支 → GitHub 建 PR（写明做了什么 + 怎么验证）→ 对方评审 → 满足 5 条后 Squash and merge 到 main → 删远端分支。
4. **评审 5 条**：① 构建 + ctest 全绿 ② format_check 通过 ③ 改了 `/api` 字段时 contract.md 已同步且 PR 描述标注「接口变更」并 @ 对方 ④ 新功能带单测或 smoke 步骤 ⑤ 一个 PR 只做一件事。
5. **接口变更**：先改 `docs/contract.md` 再改代码，同 PR 提交，对方确认后才能合并。
6. **同步节奏**：每 2-3 天与 A 对照一次 contract.md 增量；每阶段结束双方一起在 main 上跑本阶段 gate。

## 3. 前端代码地图

```
frontend/src/
├── api.js                 ★ 所有接口封装（与 contract.md 一一对应）+ usePolling 轮询 hook
├── App.vue                tab 切换四视图（不引入 vue-router/pinia，keep it simple）
├── views/
│   ├── ControlView.vue    总控台: 手动触发匹配/自动匹配开关/虚拟流启停/重置 + StatBar
│   ├── PoolView.vue       匹配池表格: passenger_id/party_size/gender/时间窗/状态 + 同意/拒绝按钮
│   ├── GroupsView.vue     成团卡片列表 + 完成按钮
│   └── EventsView.vue     事件流(since_id 增量轮询, 彩色 tag 区分 event_type)
└── components/
    ├── PassengerForm.vue  手动录入/修改表单（字段名与 API 完全一致）
    ├── VirtualControl.vue 虚拟生成参数表单
    ├── GroupCard.vue / EventFeed.vue / StatBar.vue
```

api.js 用法（已写好的 P0 代码，直接用）：

```js
import { api, usePolling } from './api'
const pool = await api.pool()                    // 直接返回 data 部分，code!==0 自动 throw
const { data, error } = usePolling(() => api.events(lastId), 2000)  // 组件内轮询，卸载自动停止
```

mock 先行（P1 用）：后端接口未完成时，在视图里先写死假数据数组（**字段名严格照 contract.md**）渲染，注释 `// TODO(P2): 接 api.xxx()`，P2 统一替换。

## 4. 分阶段任务

### P0 环境脚手架 —— ✅ 已完成（发起人完成，供你理解）

交付：Vite 工程、`/api` 代理、健康状态页、`api.js` 全接口封装 + usePolling。G0 已通过，证据见 `docs/test_report.md`。你的工作从 P1 开始。

### P1 前端静态视图（mock 先行，5-7 天）

- **触发时间**：完成第 1 节环境准备。
- **交付物**：`views/PoolView.vue`、`views/GroupsView.vue` 静态版（App.vue 加 tab 切换）；与 A 逐字段核对 contract.md 的乘客字段（**你负责维护契约文档**）。
- **逐项检查（AC）**：字段名与 contract.md 完全一致；mock 数据渲染正确；`npm run build` 退出码 0；本阶段末 A/B 一起确认 contract.md 字段，由 A 的 P1 PR 把版本头改 `1.0`、status 改 `FROZEN`。
- **通过产物**：静态视图 PR 合入 main；契约冻结达成。
- **失败路径**：静态视图未完成前不开始 P2 表单组件。

### P2 smoke 脚本 + 状态机文档 + 表单组件（5-7 天）

- **触发时间**：A 的 P2 PR 合入 main（后端 17 端点可用）。
- **交付物**：`scripts/smoke_api.sh`（按 contract.md 逐条 curl 断言，成功路径 code=0 / 预期错误码，与 A 合写）；`docs/state_machine.md`（ASCII 状态图 + 转移表，与 A 代码逐条一致，A 校对）；`components/PassengerForm.vue`、`VirtualControl.vue` 接真实接口（ControlView 可录入乘客 / 生成虚拟乘客）。
- **逐项检查（AC）**：smoke 在 A 与你的机器上都全绿；state_machine.md 与实现一致；表单提交后池视图出现新乘客。
- **通过产物**：smoke + 文档 + 表单 PR 合入 main。
- **失败路径**：smoke 未全绿前不开始 P3 事件流轮询。

### P3 四视图 + 事件流 + 一键演示（5-7 天）

- **交付物**：`views/EventsView.vue`（since_id 增量轮询）+ `EventFeed`；ControlView 完整化（自动匹配 / 虚拟流开关）；PoolView 同意/拒绝按钮；`scripts/run_demo.sh`（起后端 → 起前端 → 启动虚拟流 → 开启自动匹配 → 成团演示）。
- **逐项检查（AC）**：
  - AC-3.1 手动流程：GUI 录入 4 名乘客 → 触发匹配 → 点同意 → 全部同意 → 成团卡片出现（截图入测试报告）；
  - AC-3.2 一键 run_demo：池/团/事件三视图实时刷新；浏览器 console 无报错；无轮询死循环（网络面板请求间隔均匀）；
  - AC-3.3 后端未启动时页面显示"后端未连接"提示；
  - `npm run build` 退出码 0。
- **通过产物**：双方交叉验收（A 跑通 run_demo，你跑通 ctest+smoke）记录入 test_report.md；四视图截图入测试报告。
- **失败路径**：交叉验收不过，继续修本分支。

### P4 实验 + 四份文档（5-7 天，弹性）

- **交付物**：`docs/experiments.md`（固定种子 100 乘客分别跑 `/api/match/trigger` 与 `/api/match/optimize`，收集：平均每车人数 / 成团率 / 池大小 / 偏好满足率，表格 + 结论）；`docs/taskbook.md` 定稿；`docs/test_report.md` 定稿（G0-G4 全部 AC 证据）；`docs/final_report.md` 定稿。
- **逐项检查（AC）**：AC-4.1 实验数据表（optimize ≥ 贪心，由 A 提供 optimize 接口）；AC-4.3 四份文档互相引用、证据带 AC 编号、结题报告含状态机图 + 实验对比表 + 分工说明。
- **通过产物**：四份文档 PR 合入 main。

#### 文档写作指引（你负责的四份）

- **任务书 `docs/taskbook.md`**（P1 末定稿、P4 补工作量）：仿哈工大格式三要素——① 已知技术参数和设计要求（技术栈 / 业务规则 / 状态机 / 数据约定）② 工作量（两人分工，每人模块清单）③ 工作计划安排（P0-P4 表：阶段 / 内容 / A 任务 / B 任务 / 验收）。
- **测试报告 `docs/test_report.md`**（已有 G0 骨架）：每阶段追加 | AC | 内容 | 结果 | 证据 | 表；证据 = 真实命令 + 真实输出摘录，不许写"大概通过"；截图放 `docs/images/`（自建目录）。
- **结题报告 `docs/final_report.md`**：成果总结 / 系统架构与模块 / 状态机图 / 实验对比表（引 experiments.md）/ 理论结论（引 design.md）/ 分工说明。
- **实验记录 `docs/experiments.md`**：固定 seed 与参数必须写清楚（他人可复现）；指标表格 + 一句结论。
- 一律中文；图用 ASCII 或 mermaid（GitHub 可直接渲染）或截图。

## 5. 遇到问题怎么办

1. 先查 `docs/contract.md`（接口/字段/错误码）与 `docs/design.md`（架构）。
2. 再找对方：GitHub Issues 开 issue 并 @ 对方。
3. 仍无法解决：找项目发起人（任务书撰写者）讨论；**不得自行扩大需求范围**——复杂需求记入 design.md「未来工作」。
