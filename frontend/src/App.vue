<script setup>
// P0 脚手架：首页显示后端健康状态（G0 验收项③）
import { api, usePolling } from './api'

const { data: health, error } = usePolling(api.health, 2000)
</script>

<template>
  <main>
    <h1>机场拼车匹配工具</h1>
    <p class="status" v-if="health">
      后端已连接（运行 {{ Math.round(health.uptime_ms / 1000) }} 秒）
    </p>
    <p class="status err" v-else-if="error">后端未连接：{{ error }}</p>
    <p class="status" v-else>正在连接后端…</p>
    <p class="hint">P0 脚手架验证页。P3 起替换为管理台四视图（总控台 / 匹配池 / 成团 / 事件流）。</p>
  </main>
</template>

<style scoped>
main {
  max-width: 720px;
  margin: 48px auto;
  font-family: system-ui, sans-serif;
}
.status {
  font-size: 18px;
  padding: 12px 16px;
  border-radius: 8px;
  background: #e8f5e9;
}
.status.err {
  background: #ffebee;
}
.hint {
  color: #888;
}
</style>
