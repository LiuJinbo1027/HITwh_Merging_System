import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// 开发服务器代理：前端只写相对路径 /api/...，由 Vite 转发到后端 8080（零 CORS 代码）
export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8080',
        changeOrigin: true,
      },
    },
  },
})
