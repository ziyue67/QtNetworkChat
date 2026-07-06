import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const __dirname = path.dirname(fileURLToPath(import.meta.url))

// @ts-expect-error process is a nodejs global
const tauriDevHost: string | undefined = process.env.TAURI_DEV_HOST

export default defineConfig(async () => ({
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src')
    }
  },
  clearScreen: false,
  server: {
    port: 1420,
    strictPort: true,
    // 绑定到 127.0.0.1 避免 WebView2 解析 localhost 到 ::1 导致拒绝访问
    host: tauriDevHost || '127.0.0.1',
    allowedHosts: tauriDevHost ? [tauriDevHost, '127.0.0.1', 'localhost'] : ['127.0.0.1', 'localhost'],
    hmr: tauriDevHost
      ? {
          protocol: 'ws',
          host: tauriDevHost,
          port: 1421
        }
      : undefined,
    watch: {
      ignored: ['**/src-tauri/**']
    }
  }
}))
