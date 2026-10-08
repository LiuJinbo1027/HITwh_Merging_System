<script setup>
// 管理台外壳：顶栏（标题 + 后端状态徽章）+ 四视图 tab 切换。
// 不引入 vue-router/pinia（手册约定 keep it simple）：tab 用一个响应式 ref 记住当前页。
import { ref } from 'vue'
import { api, usePolling } from './api'
import ControlView from './views/ControlView.vue'
import PoolView from './views/PoolView.vue'
import GroupsView from './views/GroupsView.vue'
import EventsView from './views/EventsView.vue'

const tabs = [
  { key: 'control', label: '总控台' },
  { key: 'pool', label: '匹配池' },
  { key: 'groups', label: '成团' },
  { key: 'events', label: '事件流' },
]

// 支持 URL 哈希直达某视图（如 /#events）：run_demo 直达总控台、验收截图用
function tabFromHash() {
  const h = window.location.hash.slice(1)
  return tabs.some((t) => t.key === h) ? h : 'pool'
}
const activeTab = ref(tabFromHash())
window.addEventListener('hashchange', () => {
  activeTab.value = tabFromHash()
})

// 后端健康状态（沿用 G0 检查；P1 mock 阶段后端未启动不影响静态视图渲染）
const { data: health, error } = usePolling(api.health, 1000)
</script>

<template>
  <header class="topbar">
    <h1>机场拼车匹配工具</h1>
    <span class="status" :class="health ? 'ok' : error ? 'err' : ''">
      {{
        health
          ? `后端已连接 · 运行 ${Math.round(health.uptime_ms / 1000)}s`
          : error
            ? '后端未连接'
            : '连接中…'
      }}
    </span>
  </header>
  <nav class="tabs">
    <button
      v-for="t in tabs"
      :key="t.key"
      class="tab"
      :class="{ active: activeTab === t.key }"
      @click="activeTab = t.key"
    >
      {{ t.label }}
    </button>
  </nav>
  <!-- v-if 按需挂载：只有当前 tab 的组件存在（P3 轮询只在当前视图跑） -->
  <main class="content">
    <ControlView v-if="activeTab === 'control'" />
    <PoolView v-else-if="activeTab === 'pool'" />
    <GroupsView v-else-if="activeTab === 'groups'" />
    <EventsView v-else />
  </main>
</template>

<style scoped>
.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 24px;
  background: var(--surface);
  border-bottom: 1px solid var(--border);
}
.topbar h1 {
  margin: 0;
  font-size: 16px;
}
.status {
  font-size: 12px;
  padding: 3px 10px;
  border-radius: 999px;
  background: var(--tag-waiting-bg);
  color: var(--tag-waiting-fg);
}
.status.ok {
  background: var(--tag-grouped-bg);
  color: var(--tag-grouped-fg);
}
.status.err {
  background: var(--tag-cancelled-bg);
  color: var(--tag-cancelled-fg);
}
.tabs {
  display: flex;
  gap: 4px;
  padding: 0 24px;
  background: var(--surface);
  border-bottom: 1px solid var(--border);
}
.tab {
  padding: 10px 16px;
  border: none;
  background: none;
  font-size: 14px;
  color: var(--muted);
  cursor: pointer;
  border-bottom: 2px solid transparent;
}
.tab:hover {
  color: var(--text);
}
.tab.active {
  color: var(--primary);
  border-bottom-color: var(--primary);
  font-weight: 600;
}
.content {
  max-width: 1100px;
  margin: 0 auto;
  padding: 20px 24px 48px;
}
</style>
