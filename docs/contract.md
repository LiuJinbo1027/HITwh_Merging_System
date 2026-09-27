# 拼车匹配工具 · 接口契约（唯一事实源）

- schema_version: 1.0-draft
- status: DRAFT（P1 末冻结 v1，此后字段变更须双人确认、先改本文档再改代码）
- 通信：HTTP + JSON，前端经 Vite dev proxy 访问 `/api/*` → `http://127.0.0.1:8080`

## 0. 全局约定

1. 统一响应信封：成功 `{"code":0,"message":"ok","data":...}`；失败 `code` 非 0（见错误码表）。
2. 字段全部 snake_case；时间表示：`date` 字符串 `"YYYY-MM-DD"` + `start_min`/`end_min`（0-1440 整数）。
   冻结规则：单日、单方向（市区→机场）、`start_min ≤ end_min`（跨午夜不支持）。
3. 时间窗重叠判定（可同车条件）：`max(start_min) ≤ min(end_min)`。

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
| POST | `/api/match/optimize` | FR-17（P4 弹性）全局优化匹配，返回同 trigger |
| GET | `/api/match/pool` | FR-10 → `{passengers:[...]}`（waiting 状态，按 start_min 升序） |
| GET | `/api/match/groups` | FR-11 → `{groups:[{group_id, member_ids, depart_min, formed_at_ms}]}` |
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

## 6. 状态机（详情见 docs/state_machine.md，P2 定稿）

```
waiting →(引擎提案)→ proposed →(全员同意)→ grouped →(到点/手动)→ completed
任意态 →(cancel)→ cancelled；waiting/proposed/grouped →(改时间)→ 重入桶/解散提案/解散团
proposed →(拒绝/超时)→ 全体回 waiting 重匹配
```
