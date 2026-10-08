// format.js：展示层格式化函数。
// 契约数据原样保存（snake_case 字段 / start_min 分钟数），只在渲染时转换成人可读格式——
// 「数据与展示分离」：改显示格式不碰数据，接真实接口时这些函数一个都不用动。

// 分钟数 → "HH:MM"（如 510 → "08:30"，契约 start_min/end_min/depart_min 的展示）
export function fmtMin(m) {
  const hh = String(Math.floor(m / 60)).padStart(2, '0')
  const mm = String(m % 60).padStart(2, '0')
  return `${hh}:${mm}`
}

// 毫秒时间戳 → 本地时间 "HH:MM:SS"（如 formed_at_ms）
export function fmtTime(ms) {
  return new Date(ms).toLocaleTimeString('zh-CN', { hour12: false })
}

// 日期 "YYYY-MM-DD" → "MM-DD"（表格场景年份冗余，缩短行宽）
export function fmtDateShort(d) {
  return d.slice(5)
}

// 表单输入 "HH:MM" → 分钟数（契约 start_min/end_min 的录入换算；fmtMin 的逆运算）
export function timeToMin(hhmm) {
  const [h, m] = hhmm.split(':').map(Number)
  return h * 60 + m
}

// 本地时区今天的 "YYYY-MM-DD"（表单 date 字段默认值；不用 toISOString——那是 UTC，凌晨会差一天）
export function todayStr() {
  const d = new Date()
  const pad = (n) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`
}
