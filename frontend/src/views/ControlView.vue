<script setup>
// 总控台（P3 完整化）：顶部 StatBar（FR-13 stats 轮询）+ 匹配控制（FR-4 手动触发 /
// FR-15 auto_match_interval_ms 开关 / FR-16 重置）+ 虚拟流启停（FR-9）+ 表单区
// （PassengerForm FR-1 / VirtualControl FR-8，P2 已接真实接口）。
import { ref } from 'vue'
import { api, usePolling } from '../api'
import PassengerForm from '../components/PassengerForm.vue'
import VirtualControl from '../components/VirtualControl.vue'

// ---- StatBar：独立 2s 轮询统计（契约 FR-13 五个指标） ----
const { data: stats } = usePolling(() => api.stats(), 2000)

// ---- 匹配控制 ----
// 自动匹配开关的当前值来自 /api/config 轮询（后端是唯一事实源，页面刷新/重置后状态自动纠正）
const { data: config } = usePolling(() => api.getConfig(), 3000)
const matchMsg = ref(null)

async function onTrigger() {
  matchMsg.value = null
  try {
    const data = await api.triggerMatch()
    matchMsg.value = {
      type: data.proposals.length ? 'ok' : 'err',
      text: data.proposals.length
        ? `本轮产生 ${data.proposals.length} 个提案，到匹配池点「同意」`
        : '本轮无新提案：池中可匹配乘客不足（时间窗不重叠或人数不够）',
    }
  } catch (e) {
    matchMsg.value = { type: 'err', text: `触发失败：${e.message}` }
  }
}

async function onToggleAutoMatch() {
  matchMsg.value = null
  const on = !(config.value && config.value.auto_match_interval_ms > 0)
  try {
    await api.setConfig({ auto_match_interval_ms: on ? 5000 : 0 })
    // 配置轮询 3s 内会自动更新开关显示，这里不手动改
    matchMsg.value = { type: 'ok', text: on ? '自动匹配已开启（每 5s 一轮）' : '自动匹配已关闭' }
  } catch (e) {
    matchMsg.value = { type: 'err', text: `切换失败：${e.message}` }
  }
}

async function onReset() {
  if (!window.confirm('重置将清空池/提案/团/事件流并恢复默认配置，确定？')) return
  matchMsg.value = null
  streamMsg.value = null
  try {
    await api.reset()
    streamRunning.value = false // 重置同时停掉虚拟流
    matchMsg.value = { type: 'ok', text: '已重置：全部状态清空、配置回默认' }
  } catch (e) {
    matchMsg.value = { type: 'err', text: `重置失败：${e.message}` }
  }
}

// ---- 虚拟流启停（FR-9） ----
const streamForm = ref({ interval_ms: 1000, batch_size: 3 })
const streamRunning = ref(false)
const streamMsg = ref(null)

async function onStreamStart() {
  streamMsg.value = null
  try {
    await api.streamStart({
      interval_ms: streamForm.value.interval_ms,
      batch_size: streamForm.value.batch_size,
    })
    streamRunning.value = true
    streamMsg.value = { type: 'ok', text: '虚拟流已启动（首批立即生成，此后每间隔一批）' }
  } catch (e) {
    streamMsg.value = { type: 'err', text: `启动失败：${e.message}` }
  }
}

async function onStreamStop() {
  streamMsg.value = null
  try {
    await api.streamStop()
    streamRunning.value = false
    streamMsg.value = { type: 'ok', text: '虚拟流已停止' }
  } catch (e) {
    streamMsg.value = { type: 'err', text: `停止失败：${e.message}` }
  }
}
</script>

<template>
  <section class="control">
    <header class="head">
      <h2>总控台</h2>
      <p class="hint">P3：统计条 + 触发匹配 / 自动匹配开关 / 虚拟流启停 / 重置 + 录入与生成表单。</p>
    </header>

    <!-- StatBar（FR-13）：后端未通时显示占位符，通了自动填数 -->
    <div class="statbar card">
      <div class="stat">
        <span class="stat-label">池大小</span>
        <span class="stat-value">{{ stats ? stats.pool_size : '—' }}</span>
      </div>
      <div class="stat">
        <span class="stat-label">进行中团</span>
        <span class="stat-value">{{ stats ? stats.group_count : '—' }}</span>
      </div>
      <div class="stat">
        <span class="stat-label">平均每车人数</span>
        <span class="stat-value">{{ stats ? stats.avg_group_size.toFixed(2) : '—' }}</span>
      </div>
      <div class="stat">
        <span class="stat-label">女性占比</span>
        <span class="stat-value">{{ stats ? (stats.female_ratio * 100).toFixed(0) + '%' : '—' }}</span>
      </div>
      <div class="stat">
        <span class="stat-label">偏好满足率</span>
        <span class="stat-value">
          {{ stats ? (stats.gender_pref_satisfied_ratio * 100).toFixed(0) + '%' : '—' }}
        </span>
      </div>
    </div>

    <div class="grid">
      <section class="card">
        <h3 class="card-title">匹配控制</h3>
        <div class="row">
          <button class="btn primary" @click="onTrigger">手动触发匹配</button>
          <label class="switch">
            <input
              type="checkbox"
              :checked="config && config.auto_match_interval_ms > 0"
              :disabled="!config"
              @change="onToggleAutoMatch"
            />
            自动匹配{{ config && config.auto_match_interval_ms > 0 ? '（每 5s）' : '' }}
          </label>
          <button class="btn danger" @click="onReset">重置</button>
        </div>
        <p v-if="matchMsg" class="msg" :class="matchMsg.type">{{ matchMsg.text }}</p>
      </section>

      <section class="card">
        <h3 class="card-title">虚拟流（stream）</h3>
        <div class="row">
          <label class="mini-field">
            间隔 ms
            <input v-model.number="streamForm.interval_ms" type="number" min="100" max="60000" />
          </label>
          <label class="mini-field">
            每批人数
            <input v-model.number="streamForm.batch_size" type="number" min="1" max="1000" />
          </label>
          <button class="btn primary" :disabled="streamRunning" @click="onStreamStart">启动</button>
          <button class="btn" :disabled="!streamRunning" @click="onStreamStop">停止</button>
        </div>
        <p v-if="streamMsg" class="msg" :class="streamMsg.type">{{ streamMsg.text }}</p>
      </section>

      <PassengerForm />
      <VirtualControl />
    </div>
  </section>
</template>

<style scoped>
.grid {
  margin-top: 12px;
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(340px, 1fr));
  gap: 12px;
  align-items: start;
}
.statbar {
  margin-top: 12px;
  display: flex;
  flex-wrap: wrap;
  gap: 28px;
  padding: 12px 20px;
}
.stat {
  display: flex;
  flex-direction: column;
}
.stat-label {
  font-size: 12px;
  color: var(--muted);
}
.stat-value {
  font-size: 20px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}
.card-title {
  margin: 0 0 12px;
  font-size: 14px;
}
.row {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
}
.switch {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  cursor: pointer;
}
.mini-field {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: var(--muted);
}
.mini-field input {
  width: 90px;
  padding: 6px 10px;
  border: 1px solid var(--border);
  border-radius: 6px;
  font-size: 14px;
}
.mini-field input:focus {
  outline: none;
  border-color: var(--primary);
}
.msg {
  margin: 10px 0 0;
  font-size: 13px;
}
.msg.ok {
  color: var(--tag-grouped-fg);
}
.msg.err {
  color: var(--tag-cancelled-fg);
}
</style>
