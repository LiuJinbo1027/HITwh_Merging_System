# 拼车匹配工具 · 接口契约（唯一事实源）

- schema_version: 1.0
- status: FROZEN（P1 末冻结 v1.0；P2 仅补充语义澄清、无字段增删；此后字段变更须双人确认、先改本文档再改代码）
- 机器可读镜像：[contract.yaml](contract.yaml)（供 AI/工具解析；两处不一致以本文件为准）
- 通信：HTTP + JSON，前端经 Vite dev proxy 访问 `/api/*` → `http://127.0.0.1:8080`

## 0. 全局约定

1. 统一响应信封：成功 `{"code":0,"message":"ok","data":...}`；失败 `code` 非 0（见错误码表）。
2. 字段全部 snake_case；时间表示：`date` 字符串 `"YYYY-MM-DD"` + `start_min`/`end_min`（0-1440 整数）。
   冻结规则：单日、单方向（市区→机场）、`start_min ≤ end_min`（跨午夜不支持）。
3. 时间窗重叠判定（可同车条件）：`max(start_min) ≤ min(end_min)`。
4. 失败响应 HTTP 状态码统一 400（成功 200）；业务语义以信封内 `code` 为准，前端按 `code` 分支。

## 1. 乘客字段（API、状态机、前端表单三处一致）

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| passenger_id | int | 响应 | 全局唯一自增 |
| party_size | int | 是 | 1-4 人 |
| gender | string | 是 | `male` \| `female` |
| gender_preference | string | 是 | `none` \| `female_only` \| `male_only`；语义：同车其他成员须全为所偏好性别 |
| date | string | 是 | `"YYYY-MM-DD"` |
| start_min | int | 是 | 0-1440 |
| end_min | int | 是 | 0-1440，≥ start_min |
| status | string | 响应 | `waiting` \| `proposed` \| `grouped` \| `completed` \| `cancelled` |
| proposal_id | int/null | 响应 | 当前提案 |
| group_id | int/null | 响应 | 当前团 |
| is_virtual | bool | 响应 | 虚拟乘客为 true（自动同意提案） |

## 2. HTTP API 清单（17 端点）

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/api/passengers` | FR-1 注册拼车者 → `{passenger_id, status:"waiting"}` |
| PUT | `/api/passengers/{id}` | FR-2 修改任意子集（改时间按状态机转移） |
| DELETE | `/api/passengers/{id}` | FR-3 取消（团内则团解散其余回池） |
| POST | `/api/passengers/{id}/agree` | FR-6 同意提案；全员同意则成团 → `{proposal_id, proposal_state}` |
| POST | `/api/passengers/{id}/reject` | FR-6 拒绝提案；提案解散回池 → `{proposal_id, proposal_state:"dissolved"}` |
| POST | `/api/match/trigger` | FR-4 手动触发一轮贪心匹配 → `{proposals:[{proposal_id, member_ids, depart_min}]}` |
| POST | `/api/match/optimize` | FR-17（P4 弹性）全局优化匹配，返回同 trigger（P2 为贪心回退，见澄清） |
| GET | `/api/match/pool` | FR-10 → `{passengers:[...]}`（非终态乘客：waiting / proposed / grouped，按 start_min 升序） |
| GET | `/api/match/groups` | FR-11 → `{groups:[{group_id, member_ids, depart_min, formed_at_ms}]}`（仅进行中的团；completed / dissolved 即时移除） |
| GET | `/api/events?since_id=0&limit=100` | FR-12 → `{next_since_id, events:[...]}` |
| GET | `/api/stats` | FR-13 → `{pool_size, group_count, avg_group_size, female_ratio, gender_pref_satisfied_ratio}` |
| POST | `/api/groups/{id}/complete` | FR-14 行程完成 → `{group_id, status:"completed"}` |
| POST | `/api/virtual/generate` | FR-8 → `{generated:[passenger_id...]}` |
| POST | `/api/virtual/stream/start` | FR-9 → `{running:true}` |
| POST | `/api/virtual/stream/stop` | FR-9 → `{running:false}` |
| GET/PUT | `/api/config` | FR-15 TTL、自动匹配/生成参数，即改即生效 |
| POST | `/api/reset` | FR-16 清空全部状态 |
| GET | `/api/health` | 联调 → `{uptime_ms}` |

### 请求/响应示例

```jsonc
// POST /api/passengers
{"party_size":2,"gender":"female","gender_preference":"female_only",
 "date":"2026-09-28","start_min":510,"end_min":570}
// → {"code":0,"message":"ok","data":{"passenger_id":42,"status":"waiting"}}

// GET /api/events?since_id=100&limit=50
{"code":0,"message":"ok","data":{"next_since_id":120,"events":[
  {"event_id":101,"event_type":"proposed","payload":{"proposal_id":7,"member_ids":[42,17,9],"depart_min":525},"ts_ms":1758940000000},
  {"event_id":102,"event_type":"agreed","payload":{"passenger_id":42,"proposal_id":7},"ts_ms":1758940003000}]}}
```

### P2 语义澄清（接口澄清；无字段增删）

- `POST /api/virtual/generate`：body `{"count":3,"date":"2030-06-01"}`；`count` 1-1000（缺省取
  `stream_batch_size`），`date` 可选（缺省为服务器当天）；分布参数固定（高峰窗、人数/性别/偏好比例，
  见 `backend/src/virtual_source.cpp`），不随请求改变。
- `POST /api/virtual/stream/start`：body `{"interval_ms":1000,"batch_size":3}`，两项均可选（缺省取
  config）；启动时立即生成首批（满足「启动后 10s 内出现 created」），此后每 `interval_ms` 一批；
  运行中重复启动 → 40903。
- `POST /api/match/optimize`：P2 阶段等价回退贪心（P4 替换为全局优化，n>30 同样回退）；返回结构与
  `trigger` 相同，且提案同样进入状态机登记（返回的 proposal 可直接 agree）。
- `GET /api/events`：`next_since_id` = 本次返回的最后一条事件 id（无新事件时回显 `since_id`，前端下次
  轮询直接透传）；`limit` 收敛到 `[1,1000]`，`since_id` 不小于 0。
- `GET /api/stats` 字段语义：`pool_size` = 非终态（waiting/proposed/grouped）乘客数；`group_count` =
  进行中的团数；`avg_group_size` = 进行中团平均每车人数（Σparty_size ÷ 团数，无团为 0）；
  `female_ratio` = 非终态乘客中 female 占比（空池为 0）；`gender_pref_satisfied_ratio` =
  proposed/grouped 中带偏好乘客「同车其他成员满足其偏好」的比例（分母为 0 时约定 1.0）。
- `POST /api/reset`：清空池/提案/团/事件/虚拟流，配置回默认、随机序列复位；**事件 id 不倒退**——
  reset 后第一条事件即 `reset` 且 id 大于此前所有事件，客户端旧 `since_id` 不失效。
- 到点自动完成：按服务器本地时区把 `date + depart_min` 换算成时刻，`now ≥ 该时刻` 时自动 completed；
  仅对「成团时刻早于出发时刻」的团生效，成团时已过出发时刻的团保持 grouped 等待手动 complete
  （避免演示/补录场景中团瞬间消失）。

## 3. event_type 枚举

`created / updated / cancelled / proposed / agreed / rejected / proposal_expired / group_formed / group_dissolved / completed / virtual_generated / stream_started / stream_stopped / config_changed / auto_match_started / auto_match_stopped / reset`

事件结构：`{event_id, event_type, payload, ts_ms}`；event_id 全局单调递增，前端以 since_id 增量轮询。

## 4. 错误码

| code | 含义 | 示例 |
| --- | --- | --- |
| 40001 | 参数缺失/越界 | party_size=5、start_min=-1 |
| 40401 | 资源不存在 | 未知 passenger_id |
| 40901 | 状态冲突 | 对 waiting 乘客 agree、对已取消者改时间 |
| 40902 | 时间窗非法 | start_min > end_min |
| 40903 | 重复启动 | 虚拟流已在运行 |
| 50001 | 内部错误 | 兜底 |

## 5. 配置项（GET/PUT /api/config）

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| proposal_ttl_ms | 60000 | 同意等待超时（FR-7） |
| virtual_agree_prob | 0.9 | 虚拟乘客自动同意概率（拒绝以演示重匹配） |
| virtual_agree_delay_ms | 2000 | 虚拟乘客同意前思考延迟 |
| auto_match_interval_ms | 5000 | 自动匹配周期（0=关闭） |
| stream_interval_ms | 3000 | 虚拟流生成周期 |
| stream_batch_size | 5 | 每批生成人数 |

PUT 支持任意子集且即改即生效（如 `proposal_ttl_ms` 立即约束新提案的超时）；`auto_match_interval_ms`
在 0 ↔ 非 0 之间切换时记录 `auto_match_started` / `auto_match_stopped` 事件。

## 6. 状态机（详情见 docs/state_machine.md，P2 定稿）

```
waiting →(引擎提案)→ proposed →(全员同意)→ grouped →(到点/手动)→ completed
任意态 →(cancel)→ cancelled
waiting/proposed/grouped →(修改任意字段)→ 重入桶 / 解散提案 / 解散团
proposed →(拒绝/超时)→ 全体回 waiting 重匹配
```

注 1：proposed/grouped 上的修改不限于改时间——改人数、性别等任何字段都解散提案/团后再按新字段重入
（否则破坏容量与性别偏好不变量）；其余成员一律回 waiting。
注 2：`/api/match/groups` 仅返回进行中的团——completed（到点/手动完成）与 dissolved（成员取消/改时间）
的团即时从列表移除；其成员按各自终态从 pool 列表消失（completed）或回池重入（dissolved → waiting）。
注 3：`is_virtual` 乘客在提案内停留 ≥ `virtual_agree_delay_ms` 后，以 `virtual_agree_prob` 概率同意、
否则拒绝（拒绝即回池重匹配，用于演示）。
