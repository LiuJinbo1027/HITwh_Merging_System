// mock.js：P1 静态视图假数据。字段名与 docs/contract.md §1「乘客字段」逐字一致。
// 范围与 v1.0 草案 FR-10/FR-11 对齐：池 = 非终态乘客（waiting/proposed/grouped），
// groups = 仅进行中的团（completed / dissolved 即时移除）。
// 数据语义按契约规则手工校验：
//   · 时间窗重叠才可同车：max(成员 start_min) ≤ min(成员 end_min)
//   · depart_min = max(成员 start_min)
//   · 每团人数之和 ≤ 4（vehicle_capacity），gender_preference 不满足不成团
//   · 同一日（冻结规则：单日、市区→机场）
// TODO(P2): 后端 17 端点可用后删除本文件，视图改接 api.pool() / api.groups()。

// 匹配池：按 start_min 升序（契约 FR-10 的排序规则）。
// 覆盖 3 种非终态 status：grouped（1-3 团 #1）、proposed（4-5 提案 #7）、waiting（6-8）。
export const mockPool = [
  {
    passenger_id: 1, party_size: 2, gender: 'female', gender_preference: 'none',
    date: '2026-09-28', start_min: 510, end_min: 570,
    status: 'grouped', proposal_id: null, group_id: 1, is_virtual: false,
  },
  {
    passenger_id: 2, party_size: 1, gender: 'female', gender_preference: 'none',
    date: '2026-09-28', start_min: 525, end_min: 585,
    status: 'grouped', proposal_id: null, group_id: 1, is_virtual: false,
  },
  {
    passenger_id: 3, party_size: 1, gender: 'female', gender_preference: 'female_only',
    date: '2026-09-28', start_min: 540, end_min: 600,
    status: 'grouped', proposal_id: null, group_id: 1, is_virtual: false,
  },
  {
    passenger_id: 4, party_size: 1, gender: 'male', gender_preference: 'none',
    date: '2026-09-28', start_min: 555, end_min: 615,
    status: 'proposed', proposal_id: 7, group_id: null, is_virtual: false,
  },
  {
    passenger_id: 5, party_size: 2, gender: 'female', gender_preference: 'none',
    date: '2026-09-28', start_min: 560, end_min: 620,
    status: 'proposed', proposal_id: 7, group_id: null, is_virtual: false,
  },
  {
    passenger_id: 6, party_size: 1, gender: 'male', gender_preference: 'male_only',
    date: '2026-09-28', start_min: 600, end_min: 660,
    status: 'waiting', proposal_id: null, group_id: null, is_virtual: false,
  },
  {
    passenger_id: 7, party_size: 3, gender: 'female', gender_preference: 'none',
    date: '2026-09-28', start_min: 615, end_min: 675,
    status: 'waiting', proposal_id: null, group_id: null, is_virtual: false,
  },
  {
    passenger_id: 8, party_size: 1, gender: 'male', gender_preference: 'none',
    date: '2026-09-28', start_min: 630, end_min: 690,
    status: 'waiting', proposal_id: null, group_id: null, is_virtual: true,
  },
]

// 进行中的成团（契约 group 对象：group_id / member_ids / depart_min / formed_at_ms）。
// 团 1 满车 4 人；completed/dissolved 的团不在此列（FR-11 即时移除）。
export const mockGroups = [
  { group_id: 1, member_ids: [1, 2, 3], depart_min: 540, formed_at_ms: Date.parse('2026-09-28T08:50:00+08:00') },
]
