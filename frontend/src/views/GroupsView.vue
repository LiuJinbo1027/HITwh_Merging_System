<script setup>
// P1 静态版：成团卡片列表。「列表怎么排」在本视图，「单张卡片长什么样」在 GroupCard——
// 视图负责数据装配，组件负责展示，这是组件拆分的常见分工。
import { mockPool, mockGroups } from '../mock'
import GroupCard from '../components/GroupCard.vue'
// TODO(P2): 接 api.groups()。

// mock 阶段把 member_ids 展开成成员对象，卡片才能显示性别/人数等细节。
// TODO(P2) 契约核对项：/api/match/groups 目前只回 member_ids，且没有「按 id 查乘客」的
// GET 端点，真实联调时成员明细的数据来源需要与 A 确认（在 contract.md 冻结前敲定）。
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
