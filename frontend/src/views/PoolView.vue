<script setup>
// P1 静态版：先渲染 mock 假数据，把「长什么样」定下来；P2 接真实接口。
// 字段名与 docs/contract.md §1 乘客字段逐字一致（P1 验收项）。
import { mockPool } from '../mock'
import { fmtMin, fmtDateShort } from '../format'
// TODO(P2): 接 api.pool()。注意契约核对项：FR-10 的 pool 仅返回 waiting，
// 而本视图还需展示 proposed 乘客（同意/拒绝按钮的落点），数据来源待与 A 确认。

// 枚举值 → 中文标签：映射表集中管理，改文案只动这里
const STATUS_LABEL = {
  waiting: '等待中', proposed: '待确认', grouped: '已成团',
  completed: '已完成', cancelled: '已取消',
}
const GENDER_LABEL = { male: '男', female: '女' }
const PREF_LABEL = { none: '不限', female_only: '限女', male_only: '限男' }

// 「关联」列：proposed 显示提案号，grouped/completed 显示团号，其余为 —
function relationOf(p) {
  if (p.proposal_id != null) return `提案 #${p.proposal_id}`
  if (p.group_id != null) return `团 #${p.group_id}`
  return '—'
}

// 同意/拒绝：P1 静态版只占位。按钮可用性已按状态机限定（仅 proposed 可选），
// 与契约 40901「对 waiting 乘客 agree → 状态冲突」一致。
function onAgree(p) {
  // TODO(P2): await api.agree(p.passenger_id) 后刷新列表
}
function onReject(p) {
  // TODO(P2): await api.reject(p.passenger_id) 后刷新列表
}
</script>

<template>
  <section class="pool">
    <header class="head">
      <h2>匹配池</h2>
      <p class="hint">P1 静态演示数据（mock）。P2 接 /api/match/pool 实时刷新。</p>
    </header>
    <table class="table">
      <thead>
        <tr>
          <th>ID</th>
          <th>人数</th>
          <th>性别</th>
          <th>性别偏好</th>
          <th>日期</th>
          <th>时间窗</th>
          <th>状态</th>
          <th>关联</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="p in mockPool" :key="p.passenger_id">
          <td>
            <span class="num">#{{ p.passenger_id }}</span>
            <span v-if="p.is_virtual" class="tag virtual">虚拟</span>
          </td>
          <td>{{ p.party_size }}</td>
          <td>{{ GENDER_LABEL[p.gender] }}</td>
          <td>{{ PREF_LABEL[p.gender_preference] }}</td>
          <td>{{ fmtDateShort(p.date) }}</td>
          <td class="num">{{ fmtMin(p.start_min) }}–{{ fmtMin(p.end_min) }}</td>
          <td><span class="tag" :class="p.status">{{ STATUS_LABEL[p.status] }}</span></td>
          <td class="rel">{{ relationOf(p) }}</td>
          <td class="actions">
            <button class="btn" :disabled="p.status !== 'proposed'" @click="onAgree(p)">同意</button>
            <button class="btn danger" :disabled="p.status !== 'proposed'" @click="onReject(p)">拒绝</button>
          </td>
        </tr>
      </tbody>
    </table>
  </section>
</template>

<style scoped>
/* 只对本组件生效的样式（scoped），其他视图不受影响 */
.table {
  margin-top: 12px;
  width: 100%;
  border-collapse: collapse;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 10px;
  overflow: hidden;
}
.table th,
.table td {
  padding: 8px 14px;
  border-bottom: 1px solid var(--border);
  text-align: left;
}
.table thead th {
  background: #fafbfc;
  font-size: 12px;
  color: var(--muted);
  font-weight: 600;
}
.table tbody tr:last-child td {
  border-bottom: none;
}
.num {
  /* 数字等宽，行与行对齐，表格更整齐 */
  font-variant-numeric: tabular-nums;
}
.rel {
  color: var(--muted);
  font-size: 13px;
}
.actions {
  display: flex;
  gap: 6px;
}
.tag.virtual {
  margin-left: 6px;
}
</style>
