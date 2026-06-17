import { useState } from 'react'
import { useAuthStore } from '@/stores/authStore'

interface LoginViewProps {
  onLogin?: () => void
}

export function LoginView({ onLogin }: LoginViewProps) {
  const [account, setAccount] = useState('')
  const [password, setPassword] = useState('')
  const [host, setHost] = useState('127.0.0.1')
  const [port, setPort] = useState('6379')
  const [loading, setLoading] = useState(false)
  const loginStore = useAuthStore((state) => state.login)
  const setServer = useAuthStore((state) => state.setServer)

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    setLoading(true)
    setServer(host, Number(port))
    // Phase 2: mock login
    await new Promise((resolve) => setTimeout(resolve, 600))
    loginStore({
      id: 'mock-user-id',
      nickname: account || 'QQ 用户',
      status: 'online'
    })
    setLoading(false)
    onLogin?.()
  }

  return (
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] p-6">
      <form
        onSubmit={handleSubmit}
        className="w-full max-w-sm rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-8 shadow-[var(--qq-shadow)]"
      >
        <div className="mb-6 flex items-center justify-center gap-2">
          <div className="flex h-10 w-10 items-center justify-center rounded-lg bg-[var(--qq-primary)] text-lg font-bold text-white">
            Q
          </div>
          <h1 className="text-xl font-semibold text-[var(--qq-text)]">QQ NT</h1>
        </div>

        <div className="space-y-4">
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">服务器地址</label>
            <div className="flex gap-2">
              <input
                value={host}
                onChange={(e) => setHost(e.target.value)}
                className="flex-1 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
                placeholder="127.0.0.1"
              />
              <input
                value={port}
                onChange={(e) => setPort(e.target.value)}
                className="w-20 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
                placeholder="6379"
              />
            </div>
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">账号</label>
            <input
              value={account}
              onChange={(e) => setAccount(e.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
              placeholder="QQ 号 / 手机号"
            />
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">密码</label>
            <input
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
              placeholder="请输入密码"
            />
          </div>
        </div>

        <button
          type="submit"
          disabled={loading}
          className="mt-6 w-full rounded-md bg-[var(--qq-primary)] py-2.5 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-60"
        >
          {loading ? '登录中…' : '登录'}
        </button>
      </form>
    </div>
  )
}