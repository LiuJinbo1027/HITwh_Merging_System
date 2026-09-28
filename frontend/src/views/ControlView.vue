<script setup>
// P2 实现（触发条件：A 的 P2 PR 合入，后端 17 端点可用）：
// 手动触发匹配（FR-4）/ 自动匹配开关（FR-15 auto_match_interval_ms）/
// 虚拟流启停（FR-9）/ 重置（FR-16），顶部附 StatBar 统计条（FR-13，并入本视图）。
// 涉及组件：components/PassengerForm.vue（FR-1/2 录入与修改）、components/VirtualControl.vue（FR-8/9）。
</script>

<template>
  <section class="placeholder">
    <h2>总控台</h2>
    <p>P2 实现：手动触发匹配 / 自动匹配开关 / 虚拟乘客流启停 / 重置，顶部附 StatBar 统计条。</p>
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
