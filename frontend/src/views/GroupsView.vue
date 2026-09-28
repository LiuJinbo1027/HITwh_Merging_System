<script setup>
// P1 静态版：成团卡片列表。「列表怎么排」在本视图，「单张卡片长什么样」在 GroupCard——
// 视图负责数据装配，组件负责展示，这是组件拆分的常见分工。
import { mockPool, mockGroups } from '../mock'
import GroupCard from '../components/GroupCard.vue'
// TODO(P2): 接 api.groups()。

// mock 阶段把 member_ids 展开成成员对象，卡片才能显示性别/人数等细节。
// TODO(P2): 接 api.pool() + api.groups()。契约 FR-10 已放宽为返回非终态乘客
// （含 grouped），真实联调时用 pool 数据做与这里相同的 join 即可拿到成员明细。
const groups = mockGroups.map((g) => ({
  ...g,
  members: g.member_ids.map((id) => mockPool.find((p) => p.passenger_id === id)),
}))
</script>

<template>
  <section class="groups">
    <header class="head">
      <h2>已成团</h2>
      <p class="hint">P1 静态演示数据（mock）。P2 接 /api/match/groups 实时刷新。</p>
    </header>
    <div v-if="groups.length" class="grid">
      <GroupCard v-for="g in groups" :key="g.group_id" :group="g" :members="g.members" />
    </div>
    <p v-else class="empty">暂无成团。录入乘客并触发匹配后，全员同意的提案会在这里成团。</p>
  </section>
</template>

<style scoped>
.grid {
  margin-top: 12px;
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 12px;
}
.empty {
  margin-top: 12px;
  padding: 32px;
  text-align: center;
  color: var(--muted);
  background: var(--surface);
  border: 1px dashed var(--border);
  border-radius: 10px;
}
</style>
