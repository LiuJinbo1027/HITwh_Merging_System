import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// 开发服务器代理：前端只写相对路径 /api/...，由 Vite 转发到后端（零 CORS 代码）。
// 目标默认 8080；VITE_PROXY_TARGET 可覆盖（run_demo.sh 换后端端口时同步传入）。
export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': {
        target: process.env.VITE_PROXY_TARGET || 'http://127.0.0.1:8080',
        changeOrigin: true,
      },
    },
  },
})
