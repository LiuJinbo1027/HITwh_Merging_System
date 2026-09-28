<script setup>
// P3 实现（触发条件：P2 smoke 全绿后）：
// 事件流视图：api.events(sinceId) 增量轮询（FR-12，event_id 单调递增，since_id 增量拉取），
// 彩色 tag 区分 event_type（复用全局 .tag 样式），EventFeed 并入本视图。
</script>

<template>
  <section class="placeholder">
    <h2>事件流</h2>
    <p>P3 实现：事件流 since_id 增量轮询，彩色 tag 区分 event_type（17 种，见 contract.md §3）。</p>
  </section>
</template>

<style scoped>
.placeholder {
  padding: 48px 24px;
  text-align: center;
  color: var(--muted);
  background: var(--surface);
  border: 1px dashed var(--border);
  border-radius: 10px;
}
</style>
