<script setup>
// 乘客录入表单（FR-1）：字段名与 docs/contract.md §1 逐字一致，提交走 api.createPassenger。
// 只负责「收集表单 → 调接口 → 反馈结果」，不碰池数据——池视图自己轮询，录入后 2s 内自动出现新行。
// TODO(P3): 录入/修改双模式（FR-2）：加 initial prop + api.updatePassenger，由池视图「编辑」按钮触发。
import { reactive, ref } from 'vue'
import { api } from '../api'
import { timeToMin, todayStr } from '../format'

const form = reactive({
  party_size: 1,
  gender: 'male',
  gender_preference: 'none',
  date: todayStr(),
  start: '08:00', // 表单层用 "HH:MM"，提交时换算成契约的 start_min/end_min
  end: '10:00',
})

const submitting = ref(false)
const msg = ref(null) // {type:'ok'|'err', text}；提交时清空，成功/失败后写入

async function onSubmit() {
  msg.value = null
  if (timeToMin(form.start) > timeToMin(form.end)) {
    msg.value = { type: 'err', text: '出发时间不能晚于结束时间（contract 40902）' }
    return
  }
  submitting.value = true
  try {
    const data = await api.createPassenger({
      party_size: form.party_size,
      gender: form.gender,
      gender_preference: form.gender_preference,
      date: form.date,
      start_min: timeToMin(form.start),
      end_min: timeToMin(form.end),
    })
    // 保留表单值：演示时连续录入同时间窗乘客更顺手
    msg.value = { type: 'ok', text: `已录入乘客 #${data.passenger_id}，匹配池 2 秒内可见` }
  } catch (e) {
    msg.value = { type: 'err', text: `录入失败：${e.message}` }
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <section class="card">
    <h3 class="card-title">录入乘客</h3>
    <form class="form" @submit.prevent="onSubmit">
      <div class="field">
        <label for="pf-size">人数 party_size（1-4）</label>
        <input id="pf-size" v-model.number="form.party_size" type="number" min="1" max="4" required />
      </div>
      <div class="field">
        <label for="pf-date">日期 date</label>
        <input id="pf-date" v-model="form.date" type="date" required />
      </div>
      <div class="field">
        <label for="pf-gender">性别 gender</label>
        <select id="pf-gender" v-model="form.gender">
          <option value="male">男</option>
          <option value="female">女</option>
        </select>
      </div>
      <div class="field">
        <label for="pf-pref">性别偏好 gender_preference</label>
        <select id="pf-pref" v-model="form.gender_preference">
          <option value="none">不限</option>
          <option value="female_only">限女（同车其他成员须为女）</option>
          <option value="male_only">限男（同车其他成员须为男）</option>
        </select>
      </div>
      <div class="field">
        <label for="pf-start">出发时间 start_min</label>
        <input id="pf-start" v-model="form.start" type="time" required />
      </div>
      <div class="field">
        <label for="pf-end">最晚出发 end_min</label>
        <input id="pf-end" v-model="form.end" type="time" required />
      </div>
      <div class="form-actions">
        <button class="btn primary" type="submit" :disabled="submitting">
          {{ submitting ? '提交中…' : '录入乘客' }}
        </button>
        <p v-if="msg" class="form-msg" :class="msg.type">{{ msg.text }}</p>
      </div>
    </form>
  </section>
</template>

<style scoped>
.card-title {
  margin: 0 0 12px;
  font-size: 14px;
}
</style>
