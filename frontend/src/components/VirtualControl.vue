<script setup>
// 虚拟乘客批量生成表单（FR-8）：POST /api/virtual/generate {count, date}。
// 契约 P2 澄清：count 1-1000（缺省取 stream_batch_size），date 可选（缺省服务器当天），
// 分布参数固定（高峰窗、性别/偏好比例，见 backend/src/virtual_source.cpp），不随请求改变。
// TODO(P3): 虚拟流启停（FR-9 stream/start|stop）在 ControlView 完整化时加入。
import { reactive, ref } from 'vue'
import { api } from '../api'
import { todayStr } from '../format'

const form = reactive({
  count: 5, // 默认 = config.stream_batch_size（契约默认 5）
  date: todayStr(), // 与服务端缺省一致（服务器当天）
})

const submitting = ref(false)
const msg = ref(null)

async function onSubmit() {
  msg.value = null
  submitting.value = true
  try {
    const data = await api.virtualGenerate({ count: form.count, date: form.date })
    msg.value = { type: 'ok', text: `已生成 ${data.generated.length} 名虚拟乘客，匹配池 2 秒内可见` }
  } catch (e) {
    msg.value = { type: 'err', text: `生成失败：${e.message}` }
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <section class="card">
    <h3 class="card-title">生成虚拟乘客</h3>
    <form class="form" @submit.prevent="onSubmit">
      <div class="field">
        <label for="vc-count">人数 count（1-1000）</label>
        <input id="vc-count" v-model.number="form.count" type="number" min="1" max="1000" required />
      </div>
      <div class="field">
        <label for="vc-date">日期 date（缺省为服务器当天）</label>
        <input id="vc-date" v-model="form.date" type="date" required />
      </div>
      <div class="form-actions">
        <button class="btn primary" type="submit" :disabled="submitting">
          {{ submitting ? '生成中…' : '批量生成' }}
        </button>
        <p v-if="msg" class="form-msg" :class="msg.type">{{ msg.text }}</p>
      </div>
    </form>
    <p class="note">分布参数固定：虚拟乘客集中在高峰出发窗，约 1/3 带性别偏好（比例见 virtual_source.cpp）。</p>
  </section>
</template>

<style scoped>
.card-title {
  margin: 0 0 12px;
  font-size: 14px;
}
.note {
  margin: 10px 0 0;
  font-size: 12px;
  color: var(--muted);
}
</style>
