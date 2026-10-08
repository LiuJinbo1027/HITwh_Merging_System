# 拼车匹配工具 · 状态机文档

> 与实现逐条一致：`backend/src/state_machine.{h,cpp}`（B 起草，A 校对，P2 定稿）。
> 契约镜像：`docs/contract.md` §6（本文为 §6 的展开定稿）。
> 单测覆盖：`backend/tests/test_state_machine.cpp`（AC-2.2 全路径假时钟）。

## 1. 状态集合与存储

| 状态 | 含义 | 终态 | 存储位置 |
| --- | --- | --- | --- |
| `waiting` | 在池中等待匹配 | 否 | `Passenger.status` |
| `proposed` | 已被引擎放进提案，等待全员同意 | 否 | `Passenger.status` + `Passenger.proposal_id` |
| `grouped` | 已成团（进行中） | 否 | `Passenger.status` + `Passenger.group_id` |
| `completed` | 行程完成（到点自动 / 手动 complete） | 是 | `Passenger.status` |
| `cancelled` | 已取消 | 是 | `Passenger.status` |

除池内乘客对象外，StateMachine 内部还有两张表共同构成完整状态：

- `proposals_`：`proposal_id → { proposal, agreed 集合 }`（进行中的提案；成员同意时往 `agreed` 累积）
- `groups_`：`group_id → Group`（进行中的团；`completed` / `dissolved` 即时移除）

乘客对象上的 `proposal_id` / `group_id` 即指向这两张表的指针；表项移除时相应指针被清空。

## 2. ASCII 状态图

```
                   引擎提案（trigger / optimize / 自动匹配）
                   记 proposal_id、deadline = now + proposal_ttl_ms
                    ┌──────────────────────────────────────────┐
                    ▼                                          │
  ┌──────────┐  ┌──────────────┐    全员同意       ┌─────────┐  │
  │ waiting  │─▶│   proposed   │─────────────────▶│ grouped │──┼───────────┐
  └──────────┘  └──────────────┘                  └─────────┘  │           │
     ▲    ▲         │       ▲                          │   ▲   │           │
     │    │         │       └─ 同意（未全员）：幂等记入 agreed   │   │   │           │
     │    │         │           仍 proposed                 │   │   │           │
     │    │         │                                       │   │   │           │
     │    │         ├─ 拒绝 / 超时：提案解散，全体回 waiting ◀──┘   │   │           │
     │    │         │    （事件 rejected / proposal_expired）      │   │           │
     │    │         └─ 修改 / 取消：提案解散，其余回 waiting，       │   │           │
     │    │             本人按新字段重入桶 / cancelled               │   │           │
     │    │                                                        │   │           │
     │    └──── 修改：按新字段重入桶（updated）◀──────────────────────┘   │           │
     │                                                                  │           │
     │    修改 / 取消：团解散（group_dissolved），其余回 waiting，          │           │
     │    本人按新字段重入桶 / cancelled ─────────────────────────────────┘           │
     │                                                                              │
     ▼                                                                              │
  ┌────────────┐   到点自动 / 手动 complete（全体成员）    ┌───────────┐              │
  │ cancelled  │ ◀──── 任意非终态取消 ─────────────────── │ completed │ ◀────────────┘
  └────────────┘                                          └───────────┘
```

读图约定：箭头均从「本人」视角出发；「其余成员回 waiting」是转移的副作用（作用于同提案/同团其他乘客，见转移表）。`completed` / `cancelled` 上再有任何修改/取消/同意/拒绝 → 拒绝（40901）。

## 3. 转移表（与 state_machine.cpp 逐条对应）

| # | 当前态 | 触发 | 条件 / 校验 | 新态与动作 | 事件流 | 错误 |
| --- | --- | --- | --- | --- | --- | --- |
| T1 | waiting | 引擎提案（`/api/match/trigger`、`/api/match/optimize`、自动匹配） | 引擎成组条件满足（时间窗 / 容量 / 性别偏好） | proposed；记 `proposal_id`，`deadline = now + proposal_ttl_ms`（engine.cpp 计算） | `proposed`（引擎发） | — |
| T2 | waiting | PUT `/api/passengers/{id}` | 新字段合法（Service 合并校验） | waiting；按新字段重入桶 | `updated` | — |
| T3 | waiting | DELETE `/api/passengers/{id}` | — | cancelled | `cancelled` | — |
| T4 | proposed | POST `/api/passengers/{id}/agree` | 未全员同意 | 仍 proposed；记入 `agreed` 集合（**重复同意幂等**，不重复记事件） | `agreed` | 非 proposed → 40901 |
| T5 | proposed | 同上 | `agreed` 集合 = 全体成员 | grouped；`proposal_id` 清空、记 `group_id`，提案表移除 | `agreed` + `group_formed` | 同上 |
| T6 | proposed | POST `/api/passengers/{id}/reject` | — | 提案解散 → **全体（含拒绝者）** 回 waiting | `rejected` | 非 proposed → 40901 |
| T7 | proposed | tick：`now ≥ deadline_ms` | — | 提案解散 → 全体回 waiting | `proposal_expired` | — |
| T8 | proposed | PUT `/api/passengers/{id}` | 新字段合法 | 提案解散 → 其余回 waiting；本人按新字段重入桶 | `updated`（其余成员无单独事件） | 已结束 → 40901 |
| T9 | proposed | DELETE `/api/passengers/{id}` | — | 提案解散 → 其余回 waiting；本人 → cancelled | `cancelled` | 已结束 → 40901 |
| T10 | grouped | PUT `/api/passengers/{id}` | 新字段合法 | 团解散 → 其余回 waiting；本人按新字段重入桶 | `group_dissolved` + `updated` | 已结束 → 40901 |
| T11 | grouped | DELETE `/api/passengers/{id}` | — | 团解散 → 其余回 waiting；本人 → cancelled | `group_dissolved` + `cancelled` | 已结束 → 40901 |
| T12 | grouped | POST `/api/groups/{id}/complete` 或 tick 到点 | — | completed（全体成员）；团从 `groups_` 移除 | `completed` | 团不存在 → 40401 |
| T13 | completed / cancelled | 任何 修改/取消/同意/拒绝 | — | 拒绝 | — | 40901 |
| T14 | waiting | agree / reject | — | 拒绝 | — | 40901 |

说明：

- **修改不限于改时间**（契约 §6 注 1）：改人数、性别、偏好等任何字段都先解散提案/团再按新字段重入——否则破坏容量与性别偏好不变量。PUT 的「任意子集」由 Service 先合并旧值、校验（40001 / 40902）后传入 `StateMachine::update`。
- **取消的先后顺序**：`cancel` 先解散提案/团（其余成员回池），再把自己置 `cancelled`；事件只有 `cancelled` 一条（提案解散本身不额外发事件，与 T6/T7 的 `rejected`/`proposal_expired` 不同）。
- **成团时刻**：`group_formed` 事件后 `groups_` 登记 `{group_id, member_ids, depart_min, formed_at_ms}`，成员 `status=grouped`、`proposal_id` 清空、`group_id` 记当前团。

## 4. tick(now_ms) 三段自动化

`StateMachine::tick` 只做三件事，顺序固定（**now_ms 由调用方注入——测试用假时钟，禁止真实 sleep**）：

1. **提案超时**：遍历 `proposals_`，`now ≥ deadline_ms` 的提案 → 发 `proposal_expired`（payload：proposal_id + member_ids）→ 解散，全体回 waiting（T7）。
2. **虚拟乘客自动同意**（契约 §6 注 3）：遍历每个提案的成员，满足「`is_virtual == true` 且在本提案内停留 `now − created_at_ms ≥ virtual_agree_delay_ms`」者，掷骰 `[0,1)`：`< virtual_agree_prob` → agree（T4/T5），否则 reject（T6）。处理中提案被解散则跳过剩余成员。
3. **团到点自动完成**：把 `date + depart_min` 按**服务器本地时区**换算成时刻 `depart_ms`；同时满足 ①`depart_ms ≥ 0`（日期合法）②`depart_ms ≥ formed_at_ms`（**成团早于发车**——补录/演示场景中成团时已过发车时刻的团保持 grouped，等手动 complete，避免刚成团瞬间消失）③`now ≥ depart_ms` → `complete_group`（T12）。

**驱动方式**：无后台线程。Service 在每个 HTTP 请求进入时先 `tick_locked(now)`（懒驱动），到点的自动匹配与虚拟流批次也在同一函数内触发（`next_auto_match_ms_` / `next_stream_ms_` 排程）；smoke 即靠轮询 `/api/events` 驱动 tick。所有转移在 Service 单锁内执行，StateMachine 自身不加锁。

## 5. 错误码对照（状态机相关）

| 场景 | code |
| --- | --- |
| 乘客不存在（agree / reject / update / cancel） | 40401 |
| 对 waiting 乘客 agree / reject | 40901 |
| 对 grouped 乘客 agree / reject | 40901 |
| 对 completed / cancelled 乘客 update / cancel | 40901 |
| 重复取消（cancelled 再 DELETE） | 40901 |
| 团不存在或已结束（complete） | 40401 |
| 字段越界 / 时间窗非法（PUT，Service 层先拦） | 40001 / 40902 |

## 6. 事件流对照（谁发什么事件）

| event_type | 发出位置 | 触发 |
| --- | --- | --- |
| `proposed` | MatchEngine（经事件 sink） | 提案生成（T1） |
| `agreed` / `rejected` | StateMachine | T4/T5、T6 |
| `proposal_expired` | StateMachine::tick | T7 |
| `group_formed` / `group_dissolved` | StateMachine | 成团（T5）/ 团解散（T10/T11） |
| `completed` | StateMachine | 手动或到点完成（T12） |
| `cancelled` / `updated` | StateMachine | T3/T8/T9/T10/T11、T2/T8/T10 |
| `created` / `virtual_generated` / `stream_started` / `stream_stopped` / `config_changed` / `auto_match_started` / `auto_match_stopped` / `reset` | Service / 虚拟源 | 见 contract.md §3 |

## 7. reset 语义

`StateMachine::reset` 清空 `proposals_` / `groups_`、`next_group_id_ = 1`；池、事件、配置、随机序列复位由 Service 统一完成。事件 id 不倒退：reset 后第一条事件即 `reset` 且 id 大于此前所有事件（契约 §P2 澄清）。
