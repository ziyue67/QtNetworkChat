import { useState } from 'react'
import { useAuthStore } from '@/stores/authStore'
import { cn } from '@/lib/utils'

type AuthMode = 'login' | 'register'

interface LoginViewProps {
  onLogin?: () => void
}

export function LoginView({ onLogin }: LoginViewProps) {
  const [mode, setMode] = useState<AuthMode>('login')
  const [account, setAccount] = useState('')
  const [password, setPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')
  const [registeredHint, setRegisteredHint] = useState(false)
  const login = useAuthStore((state) => state.login)

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    setError('')
    setRegisteredHint(false)

    if (!account.trim() || !password.trim()) {
      setError('请输入账号和密码')
      return
    }

    if (mode === 'register' && password !== confirmPassword) {
      setError('两次输入的密码不一致')
      return
    }

    setLoading(true)

    // Phase 2 mock auth
    await new Promise((resolve) => setTimeout(resolve, 600))

    if (mode === 'register') {
      setLoading(false)
      setPassword('')
      setConfirmPassword('')
      setRegisteredHint(true)
      setMode('login')
      return
    }

    login({
      id: 'mock-user-id',
      nickname: account || 'QQ 用户',
      status: 'online'
    })

    setLoading(false)
    onLogin?.()
  }

  return (
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] py-4">
      <form
        onSubmit={handleSubmit}
        className="w-[calc(100%-32px)] rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-6 shadow-[var(--qq-shadow)]"
      >
        <div className="mb-4 flex items-center justify-center gap-2">
          <div className="flex h-9 w-9 items-center justify-center rounded-lg bg-[var(--qq-primary)] text-base font-bold text-white">
            Q
          </div>
          <h1 className="text-lg font-semibold text-[var(--qq-text)]">QQ NT</h1>
        </div>

        <div className="mb-4 flex rounded-lg bg-[var(--qq-bg-tertiary)] p-1">
          <button
            type="button"
            onClick={() => {
              setMode('login')
              setError('')
              setRegisteredHint(false)
            }}
            className={cn(
              'flex-1 rounded-md py-1.5 text-xs font-medium transition-colors',
              mode === 'login'
                ? 'bg-[var(--qq-surface)] text-[var(--qq-text)] shadow-sm'
                : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
            )}
          >
            账号密码登录
          </button>
          <button
            type="button"
            onClick={() => {
              setMode('register')
              setError('')
              setRegisteredHint(false)
            }}
            className={cn(
              'flex-1 rounded-md py-1.5 text-xs font-medium transition-colors',
              mode === 'register'
                ? 'bg-[var(--qq-surface)] text-[var(--qq-text)] shadow-sm'
                : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
            )}
          >
            注册账号
          </button>
        </div>

        <div className="space-y-3">
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
          {mode === 'register' ? (
            <div>
              <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">确认密码</label>
              <input
                type="password"
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
                placeholder="请再次输入密码"
              />
            </div>
          ) : null}
        </div>

        {error ? (
          <p className="mt-3 text-center text-xs text-[var(--qq-danger)]">{error}</p>
        ) : null}
        {registeredHint ? (
          <p className="mt-3 text-center text-xs text-[var(--qq-success)]">注册成功，请登录</p>
        ) : null}

        <button
          type="submit"
          disabled={loading}
          className="mt-4 w-full rounded-md bg-[var(--qq-primary)] py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-60"
        >
          {loading
            ? mode === 'login'
              ? '登录中…'
              : '注册中…'
            : mode === 'login'
              ? '登录'
              : '注册'}
        </button>
      </form>
    </div>
  )
}
