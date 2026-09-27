// api.js：后端接口统一封装。
// 统一信封 {code, message, data}：code===0 时 resolve(data)，否则 reject(Error(message))。
// 所有路径为相对路径 /api/...，经 Vite dev proxy 转发到后端。

const BASE = '/api'

async function request(path, options = {}) {
  const res = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  })
  const body = await res.json()
  if (body.code !== 0) {
    throw new Error(`[${body.code}] ${body.message}`)
  }
  return body.data
}

export const api = {
  health: () => request('/health'),

  // 乘客（FR-1~3）
  createPassenger: (p) => request('/passengers', { method: 'POST', body: JSON.stringify(p) }),
  updatePassenger: (id, p) =>
    request(`/passengers/${id}`, { method: 'PUT', body: JSON.stringify(p) }),
  cancelPassenger: (id) => request(`/passengers/${id}`, { method: 'DELETE' }),
  agree: (id) => request(`/passengers/${id}/agree`, { method: 'POST' }),
  reject: (id) => request(`/passengers/${id}/reject`, { method: 'POST' }),

  // 匹配（FR-4/17）
  triggerMatch: () => request('/match/trigger', { method: 'POST' }),
  optimize: () => request('/match/optimize', { method: 'POST' }),
  pool: () => request('/match/pool'),
  groups: () => request('/match/groups'),
  completeGroup: (id) => request(`/groups/${id}/complete`, { method: 'POST' }),

  // 事件与统计（FR-12/13）
  events: (sinceId = 0, limit = 100) =>
    request(`/events?since_id=${sinceId}&limit=${limit}`),
  stats: () => request('/stats'),

  // 虚拟乘客（FR-8/9）
  virtualGenerate: (p) =>
    request('/virtual/generate', { method: 'POST', body: JSON.stringify(p) }),
  streamStart: (p) =>
    request('/virtual/stream/start', { method: 'POST', body: JSON.stringify(p) }),
  streamStop: () => request('/virtual/stream/stop', { method: 'POST' }),

  // 配置与重置（FR-15/16）
  getConfig: () => request('/config'),
  setConfig: (c) => request('/config', { method: 'PUT', body: JSON.stringify(c) }),
  reset: () => request('/reset', { method: 'POST' }),
}

// usePolling：轮询 hook（P3 事件流/池/团视图使用）。
// 用法: const { data, error, stop } = usePolling(() => api.pool(), 2000)
import { ref, onMounted, onBeforeUnmount } from 'vue'

export function usePolling(fn, intervalMs) {
  const data = ref(null)
  const error = ref(null)
  let timer = null
  let stopped = false

  async function tick() {
    if (stopped) return
    try {
      data.value = await fn()
      error.value = null
    } catch (e) {
      error.value = e.message
    }
    if (!stopped) timer = setTimeout(tick, intervalMs)
  }

  onMounted(tick)
  onBeforeUnmount(() => stop())

  function stop() {
    stopped = true
    if (timer) clearTimeout(timer)
  }
  return { data, error, stop }
}
