<script setup>
// 成团卡片（独立组件之一）。父视图通过 props 传数据进来，组件只读——
// 数据单向流动（父 → 子）；P2 的「完成」操作通过事件回调传回父级。
import { computed } from 'vue'
import { fmtMin, fmtTime } from '../format'

const props = defineProps({
  group: { type: Object, required: true }, // 契约 group 对象：{group_id, member_ids, depart_min, formed_at_ms}
  members: { type: Array, required: true }, // member_ids 对应的乘客对象（由父视图用 pool 数据装配）
})
const emit = defineEmits(['complete']) // 完成操作 emit 回父级（父级调 api.completeGroup，本组件只展示）

// 派生数据用 computed：依赖变化时自动重算并缓存。
// 总人数 = Σ party_size（契约 vehicle_capacity：每团 ≤ 4 人）
const totalSize = computed(() => props.members.reduce((sum, m) => sum + m.party_size, 0))

// 注：契约 FR-11 规定 /api/match/groups 仅返回进行中的团（completed/dissolved 即时移除），
// 因此卡片不需要「已完成」状态判断，「完成」按钮恒可用。

const GENDER_LABEL = { male: '男', female: '女' }
</script>

<template>
  <article class="card group-card">
    <header class="card-head">
      <span class="gid">团 #{{ group.group_id }}</span>
      <span class="depart">出发 {{ fmtMin(group.depart_min) }}</span>
    </header>
    <ul class="members">
      <li v-for="m in members" :key="m.passenger_id">
        <span class="mid">#{{ m.passenger_id }}</span>
        {{ GENDER_LABEL[m.gender] }} · {{ m.party_size }}人
        <span v-if="m.is_virtual" class="tag virtual">虚拟</span>
      </li>
    </ul>
    <footer class="card-foot">
      <span class="meta">共 {{ totalSize }} 人 · 成团于 {{ fmtTime(group.formed_at_ms) }}</span>
      <button class="btn primary" @click="emit('complete', group)">完成</button>
    </footer>
  </article>
</template>

<style scoped>
.group-card {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.card-head {
  display: flex;
  align-items: center;
  gap: 10px;
}
.gid {
  font-weight: 600;
}
.depart {
  color: var(--muted);
  font-variant-numeric: tabular-nums;
}
.card-head .tag {
  margin-left: auto;
}
.members {
  margin: 0;
  padding: 0;
  list-style: none;
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.members li {
  padding: 2px 10px;
  border: 1px solid var(--border);
  border-radius: 999px;
  font-size: 13px;
}
.mid {
  color: var(--muted);
}
.card-foot {
  display: flex;
  align-items: center;
  justify-content: space-between;
  border-top: 1px solid var(--border);
  padding-top: 10px;
}
.meta {
  color: var(--muted);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
</style>
