<script setup>
// 事件流视图（P3）：since_id 增量轮询（FR-12）。event_id 全局单调递增，本地记住
// next_since_id，每次只拉新事件、前插到列表（最新在前）、封顶 500 条。
// 后端 reset 后事件 id 不倒退（契约 P2 澄清），旧 since_id 依然有效，无需特殊处理。
import { ref } from 'vue'
import { api, usePolling } from '../api'
import { fmtTime } from '../format'

const lastId = ref(0) // 下次轮询的 since_id
const all = ref([]) // 已拉事件（最新在前）

const { data: events, error } = usePolling(async () => {
  const d = await api.events(lastId.value, 500)
  if (d.events.length) {
    lastId.value = d.next_since_id
    // API 按 id 升序返回；倒序后前插，保持「最新在前」
    all.value = [...d.events].reverse().concat(all.value).slice(0, 500)
  }
  return all.value
}, 2000)

// 17 种 event_type → 语义色（复用全局 .tag 状态色）：生命周期/提案/成团/虚拟/配置/重置
const EVENT_STYLE = {
  created: 'waiting', updated: 'waiting', cancelled: 'cancelled',
  proposed: 'proposed', agreed: 'proposed',
  rejected: 'cancelled', proposal_expired: 'cancelled',
  group_formed: 'grouped', group_dissolved: 'cancelled', completed: 'grouped',
  virtual_generated: 'virtual',
  stream_started: 'virtual', stream_stopped: 'virtual',
  config_changed: 'completed', auto_match_started: 'completed', auto_match_stopped: 'completed',
  reset: 'cancelled',
}

// payload → 一行紧凑文本：{passenger_id:42, proposal_id:7} → "passenger_id=42  proposal_id=7"
function payloadText(p) {
  if (!p || typeof p !== 'object') return ''
  return Object.entries(p)
    .map(([k, v]) => (Array.isArray(v) ? `${k}=[${v.join(',')}]` : `${k}=${v}`))
    .join('  ')
}
</script>

<template>
  <section class="events">
    <header class="head">
      <h2>事件流</h2>
      <p class="hint">
        实时增量：/api/events 每 2 秒轮询（since_id={{ lastId }}，已显示 {{ events ? events.length : 0 }}
        条）。
      </p>
    </header>

    <p v-if="!events && !error" class="empty">加载中…</p>
    <p v-else-if="error" class="empty err">后端未连接或响应异常：{{ error }}（每 2 秒自动重试）</p>
    <p v-else-if="!events.length" class="empty">
      暂无事件。到总控台录入乘客、触发匹配或启动虚拟流，事件会在这里实时出现。
    </p>
    <ul v-else class="feed">
      <li v-for="e in events" :key="e.event_id" class="row">
        <span class="num">#{{ e.event_id }}</span>
        <span class="tag" :class="EVENT_STYLE[e.event_type] || 'completed'">{{ e.event_type }}</span>
        <span class="ts">{{ fmtTime(e.ts_ms) }}</span>
        <span class="payload">{{ payloadText(e.payload) }}</span>
      </li>
    </ul>
  </section>
</template>

<style scoped>
.feed {
  margin: 12px 0 0;
  padding: 0;
  list-style: none;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.row {
  display: flex;
  align-items: baseline;
  gap: 10px;
  padding: 8px 14px;
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 8px;
}
.num {
  color: var(--muted);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
.ts {
  color: var(--muted);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
.payload {
  font-size: 13px;
  color: var(--text);
  word-break: break-all;
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
</style>
