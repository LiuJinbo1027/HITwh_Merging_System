# 拼车匹配工具 · A 开发手册（后端与算法）

> 你的角色：**后端与算法负责人**。C++ 匹配引擎（数据结构 + 算法）是本次课程设计的核心得分点。

## 0. 职责与边界

你负责（后端）：`model / time_bucket / pool / engine / state_machine / virtual_source / event_log / service / http_server / optimizer(P4)`、后端单测、`docs/design.md` 理论章节。

队友 B 负责（前端与文档）：Vue 工程与视图组件、smoke 与演示脚本、任务书/测试报告/结题报告、实验数据。

**共同约定**：`docs/contract.md` 是接口唯一事实源——改动任何 `/api` 字段必须先改它、双人确认后同 PR 提交。

## 1. 环境准备（拿到手册后第一件事）

### 1.1 获取代码

- 仓库：https://github.com/LiuJinbo1027/HITwh_Merging_System
- 仓库主人先在 GitHub → Settings → Collaborators → Add people 把你加为协作者（否则无法推送）。
- 克隆：`git clone git@github.com:LiuJinbo1027/HITwh_Merging_System.git`（SSH，需先在 GitHub 添加你的 SSH key；或 HTTPS + 个人 token）。

### 1.2 安装工具（Linux）

- cmake ≥ 3.16、g++（支持 C++17）：`sudo apt install cmake g++`
- clang-format：`sudo apt install clang-format`；无 sudo 时 `pip install --user clang-format`（装到 `~/.local/bin`，`scripts/format_check.sh` 会自动找到）
- （可选）gh CLI 用于命令行建 PR；不会用就直接在 GitHub 网页操作。

### 1.3 构建与运行

```bash
cmake -S . -B build && cmake --build build    # 编译
./build/backend/merging_server                # 起服务，监听 127.0.0.1:8080
curl http://127.0.0.1:8080/api/health         # 应返回 {"code":0,...}
./scripts/format_check.sh                     # 格式检查（每次提交前跑）
```

## 2. 协作纪律（与 B 手册第 2 节一致，双方必须遵守）

1. **分支**：你只用 `a/<step>-<名>` 前缀（如 `a/p1-engine`）；B 用 `b/`。从最新 main 切出。
2. **提交信息**：`type(scope): summary`，如 `feat(engine): 时间桶候选查询`、`fix(api): 同意接口 40901 误报`、`test(engine): 性别偏好穷举断言`。
3. **PR 流程**：推送分支 → GitHub 建 PR（写明做了什么 + 怎么验证）→ **等 CI 全绿**（GitHub Actions 自动跑：构建 + ctest + 格式检查 + 前端构建；P2 起加 smoke）→ 对方评审 → 满足 5 条后 Squash and merge 到 main → 删远端分支。
4. **评审 5 条**：① 构建 + ctest 全绿（CI 自动把关，本地同样先自查）② format_check 通过 ③ 改了 `/api` 字段时 contract.md 已同步（含 contract.yaml 镜像）且 PR 描述标注「接口变更」并 @ 对方 ④ 新功能带单测或 smoke 步骤 ⑤ 一个 PR 只做一件事。
5. **接口变更**：先改 `docs/contract.md` 再改代码，同 PR 提交，对方确认后才能合并。
6. **同步节奏**：每 2-3 天与 B 对照一次 contract.md 增量；每阶段结束双方一起在 main 上跑本阶段 gate。

## 3. 后端代码地图

```
backend/src/
├── model.h/.cpp          领域模型: Gender/GenderPref/Status/TimeWindow/Passenger/Proposal/Group/Event
├── time_bucket.h/.cpp    TimeBucketIndex: 10 分钟粒度 144 桶, insert/remove/candidates_in_window O(1)
├── pool.h/.cpp           MatchPool: 桶索引 + std::multiset 活跃集合(扫描线), neighbors(w) 取重叠候选
├── engine.h/.cpp         MatchEngine: run_once() 贪心引擎（★核心）
├── state_machine.h/.cpp  StateMachine: on_agree/on_reject/on_cancel/on_reschedule + tick(now_ms)
├── virtual_source.h/.cpp VirtualGenerator: 随机虚拟乘客（分布集中高峰窗 + 性别偏好）
├── event_log.h/.cpp      EventLog: 追加事件 + since_id 增量查询
├── service.h/.cpp        Service: API 门面（唯一写入口，一把 std::mutex 串行）
├── http_server.h/.cpp    httplib 路由 → Service → 统一信封 {code,message,data}
└── main.cpp              入口: merging_server [端口]，默认 8080
backend/tests/            doctest 单测: test_<模块>.cpp（一个模块一个可执行，ctest 注册）
```

编码约定：

- C++17；`.clang-format` 已配好（Google + 4 缩进 + 100 列 + include 排序）。
- JSON 字段全 snake_case（对照 contract.md）。
- 所有写操作走 `Service` 单锁串行；httplib 多线程只转发请求。**不要自己起线程**。
- 时间：`date`("YYYY-MM-DD") + `start_min`/`end_min`(0-1440)；重叠判定 `max(start) ≤ min(end)`；冻结规则：单日、单方向、`start ≤ end`。
- 超时/自动行为全部走 `StateMachine::tick(now_ms)`，测试注入假时钟（now_ms 作为参数传入），**禁止 sleep 等真实时间**。
- 单测框架 doctest：`#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + `#include "doctest.h"`；测试注册进 CMake `add_test`。

## 4. 分阶段任务

### P0 环境脚手架 —— ✅ 已完成（发起人完成，供你理解）

交付：vendor 单头库（third_party：httplib v0.20.1 / nlohmann-json v3.11.3 / doctest v2.4.11）、CMake 工程、`/api/health`、统一信封、前端骨架。G0 已通过，证据见 `docs/test_report.md`。你的工作从 P1 开始。

### P1 数据结构 + 贪心引擎 + 单测（当前任务，5-7 天）

- **触发时间**：完成第 1 节环境准备。
- **交付物**：`backend/src/{model,time_bucket,pool,engine}.{h,cpp}`；`backend/tests/test_time_bucket.cpp`、`test_engine.cpp`；`backend/CMakeLists.txt` 注册 ctest。
- **逐项检查（AC）**：
  - AC-1.1 时间桶：插入/删除/窗口候选查询正确（含跨桶边界用例）。
  - AC-1.2 贪心正确性：固定输入 12 名乘客（见下方表）→ 3 个 4 人团，且每个团 `depart_min == max(成员 start_min)`。
  - AC-1.3 性别偏好（穷举断言）：`female_only` 乘客在任何提案中不与男性同车；`male_only` 同理；冲突偏好（一女性偏好男 + 一男性偏好女同团）不可行。
  - AC-1.4 效率：n=500 名 waiting 乘客 `run_once` 耗时 < 200ms。
- **通过产物**：ctest 全绿 + format_check 通过 + PR 合入 main；本 PR 内把 `docs/contract.md` 版本头改为 `1.0`、status 改 `FROZEN`（B 已确认字段）。
- **失败路径**：只能在本分支继续修；不许开始 P2 的 service/http 工作。

AC-1.2 固定输入（直接写进测试）：12 名乘客，party_size 均为 1，gender 建议 6 男 6 女均无偏好，分三组：

| 组 | start_min（4 人） | end_min | 预期 |
| --- | --- | --- | --- |
| 组1 | 500 / 510 / 520 / 530 | 600 | 成团，depart=530 |
| 组2 | 550 / 560 / 570 / 580 | 650 | 成团，depart=580 |
| 组3 | 400 / 410 / 420 / 430 | 500 | 成团，depart=430 |

断言写成"3 个 4 人团 + depart_min 集合 == {430,530,580}"（组1/组2 有重叠，具体归属取决于种子顺序，集合断言最稳）。

#### 实现指引：贪心引擎 run_once（★先读再写）

```
run_once():
  结果 proposals = []
  循环:
    从 waiting 池取种子: 优先 party_size 最大, 其次 start_min 最小
    (实现: 每轮线性选, 或 priority_queue 键 (-party_size, start_min) 惰性弹出失效项)
    若无人 waiting: break
    团 = {种子}; cap = 4 - 种子.party_size
    候选 C = pool.neighbors(种子.win) 中 status==waiting 且 party_size<=cap 者
    按 party_size 降序、start_min 升序遍历 C:
      x 能加入 iff:
        (a) 时间窗: max(团内起点 ∪ {x.start}) <= min(团内终点 ∪ {x.end})
            [Helly 性质: 区间两两相交 ⇒ 全体相交, 故只需维护这两个最值, O(1)]
        (b) 性别偏好(朴素重验, 团 ≤ 4 人成本可忽略):
            对团内每个 p:  p.pref==female_only ⇒ 团内所有人(含x) gender==female
                            p.pref==male_only   ⇒ 团内所有人 gender==male
            且 x.pref==female_only ⇒ 团内所有人 gender==female
                x.pref==male_only   ⇒ 团内所有人 gender==male
        (c) 加入后容量不超
      满足则加入
    若团人数 >= min_group_size(默认 2, 可配): 生成 Proposal(成员→proposed, 记 proposal_id,
      depart_min=max(start_min), deadline=now+TTL), 事件流记 proposed
    若该种子本轮组不出 ≥2 人团: 把它记入"本轮已尝试"集合, 换下一种子, 防止死循环
```

注意点：

- 种子处理防死循环：本轮组不成的种子不阻塞后续种子。
- `TimeWindow::overlaps / depart_min_with` 放 model.h，单测最先覆盖它们。
- P1 不写 HTTP 层：引擎直接对 `MatchPool` 操作，测试直接构造乘客数据。

### P2 HTTP 接口 + 状态机 + 虚拟乘客（5-7 天）

- **触发时间**：P1 PR 合入 main。
- **交付物**：`backend/src/{state_machine,virtual_source,event_log,service}.{h,cpp}` + http_server 路由扩展（17 个端点全实现）；`backend/tests/test_state_machine.cpp`、`test_virtual_source.cpp`、`test_service.cpp`；`scripts/smoke_api.sh`（与 B 合写）。
- **逐项检查（AC）**：
  - AC-2.1 smoke_api.sh 对 17 个端点逐一断言（成功路径 code=0 / 预期错误码）。
  - AC-2.2 状态机全路径单测（全部假时钟）：waiting→proposed→全员 agree→grouped；reject→全体回池并可再次被匹配；TTL 超时→proposal_expired 全体回池；改时间三态（池中重入桶 / 解散提案 / 解散团）；虚拟乘客在可配延迟后按 `virtual_agree_prob` 自动 agree、否则 reject。
  - AC-2.3 虚拟流：`stream/start` 后事件流在 10 秒内出现 ≥ batch_size 个 created（此条用真实时间，smoke 里测）；生成的 female_only/male_only 乘客成团后偏好满足。
  - AC-2.4 错误码：对 waiting 乘客 agree → 40901；未知 id → 40401；start_min>end_min → 40902；重复 stream/start → 40903；party_size=5 → 40001。
- **通过产物**：ctest + smoke 全绿；`docs/state_machine.md` 由 B 起草、你校对（转移表与代码逐条一致）；PR 合入 main。
- **失败路径**：继续修本分支，不允许开始 P3 前端联调。

状态机转移表（实现与文档的唯一依据）：

| 当前态 | 事件 | 新态/动作 |
| --- | --- | --- |
| waiting | 引擎提案 | proposed（记 proposal_id） |
| waiting | 改时间 | waiting（重入新桶） |
| waiting | 取消 | cancelled |
| proposed | 同意 | 全员同意→grouped（成团）；否则仍 proposed |
| proposed | 拒绝 | 提案解散→全体成员 waiting（回池可重匹配） |
| proposed | 超时 | 同上，事件记 proposal_expired |
| proposed | 改时间/取消 | 提案解散、其余成员回池；本人按新窗重入桶或 cancelled |
| grouped | 改时间/取消 | 团解散→其余成员 waiting 回池；本人重入桶或 cancelled |
| grouped | 到点(depart_min)/手动 complete | completed |

虚拟乘客自动同意（放在 StateMachine::tick 内）：`is_virtual==true` 的成员在提案内停留 ≥ `virtual_agree_delay_ms` 后，以 `virtual_agree_prob` 概率 agree，否则 reject。参数均在 `/api/config`。

### P3 配合联调（5-7 天，你这边工作量小）

- **你的职责**：修 B 联调暴露的后端 bug（事件字段、边界、返回结构）；交叉验收——在自己机器上跑通 B 的 `scripts/run_demo.sh`；在 docs/test_report.md 补你侧证据。
- **通过产物**：双方交叉验收记录写入 test_report.md。

### P4 进阶算法 + 实验 + 报告（5-7 天，弹性）

- **交付物**：`backend/src/optimizer.{h,cpp}`（n≤50 时枚举全部可行 4 人组 + 匈牙利最大权匹配；权重 = 拼满度 + 等待收益 − 性别惩罚；n>50 回退贪心）+ `test_optimizer.cpp`；`docs/design.md` 理论章节（你写）。
- **逐项检查（AC）**：
  - AC-4.1 固定种子 100 乘客：optimize 平均每车人数 ≥ 贪心、成团率不降低（数据由 B 跑实验，你提供 `/api/match/optimize`）。
  - AC-4.2 n≤10 时 optimize 与暴力枚举最优解一致（穷举所有分组，断言目标值相等）。
- **理论章节选写 2-3 个深入**：
  1. 一维 Helly 性质：区间两两相交 ⇒ 全体相交 ⇒ 4 人同车判定 O(1)；
  2. 最少车辆数下界 = 时间轴上最大重叠乘客数（区间图完美图性质）；
  3. 装箱 NP-hard + FFD 近似比，解释贪心为什么可能打不满；
  4. Gale-Shapley 稳定性概念与"稳定 vs 系统最优"权衡（说明拼车是非二分图、属稳定舍友问题，我们只借鉴其思想改造偏好约束）；
  5. 复杂度分析总表（各数据结构操作 + 算法整体复杂度）。

## 5. 遇到问题怎么办

1. 先查 `docs/contract.md`（接口/字段/错误码）与 `docs/design.md`（架构）。
2. 再找对方：GitHub Issues 开 issue 并 @ 对方。
3. 仍无法解决：找项目发起人（任务书撰写者）讨论；**不得自行扩大需求范围**——复杂需求记入 design.md「未来工作」。
