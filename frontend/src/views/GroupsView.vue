<script setup>
// P3 项提前（P2 联调暴露：池视图已显示真实成团，mock 成团卡片与真实数据冲突、误导）：
// 接 api.groups() + api.pool() 实时数据。契约 FR-10：pool 返回非终态乘客（含 grouped）；
// FR-11：groups 仅返回进行中的团（completed/dissolved 即时移除，因此列表刷新即消失）。
// 装配逻辑与 P1 mock 版一致：member_ids 展开成成员对象，卡片才能显示性别/人数等细节。
import { ref } from 'vue'
import { api, usePolling } from '../api'
import GroupCard from '../components/GroupCard.vue'

const { data: groups, error } = usePolling(async () => {
  // 一次轮询并行取两份数据（Promise.all），保证团列表与成员明细是同一时刻的快照
  const [g, pool] = await Promise.all([api.groups(), api.pool()])
  return g.groups.map((x) => ({
    ...x,
    members: x.member_ids
      .map((id) => pool.passengers.find((p) => p.passenger_id === id))
      .filter(Boolean), // 防御：极少数成员不在池快照中时跳过，不让整页渲染报错
  }))
}, 2000)

const msg = ref(null)

// 完成：卡片 emit 上来，这里调接口；成功后团从列表消失（FR-11），轮询自动刷新
async function onComplete(group) {
  msg.value = null
  try {
    await api.completeGroup(group.group_id)
  } catch (e) {
    msg.value = { type: 'err', text: `完成失败：${e.message}` }
  }
}
</script>

<template>
  <section class="groups">
    <header class="head">
      <h2>已成团</h2>
      <p class="hint">实时数据：/api/match/groups 每 2 秒轮询（仅进行中的团）。</p>
      <p v-if="msg" class="msg">{{ msg.text }}</p>
    </header>

    <p v-if="!groups && !error" class="empty">加载中…</p>
    <p v-else-if="error" class="empty err">后端未连接或响应异常：{{ error }}（每 2 秒自动重试）</p>
    <div v-else-if="groups.length" class="grid">
      <GroupCard
        v-for="g in groups"
        :key="g.group_id"
        :group="g"
        :members="g.members"
        @complete="onComplete"
      />
    </div>
    <p v-else class="empty">暂无成团。虚拟乘客在提案中会自动同意，也可等 P3 手动同意流程成团。</p>
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
.empty.err {
  color: var(--tag-cancelled-fg);
}
.msg {
  margin: 4px 0 0;
  font-size: 13px;
  color: var(--tag-cancelled-fg);
}
</style>
