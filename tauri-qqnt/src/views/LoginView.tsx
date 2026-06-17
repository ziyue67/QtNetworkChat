import { useState } from 'react'
import { cn } from '@/lib/utils'

type AuthMode = 'login' | 'register'

interface LoginViewProps {
  loading: boolean
  error?: string
  onLogin: (account: string, password: string) => Promise<void> | void
  onRegister: (account: string, password: string) => Promise<boolean>
}

export function LoginView({ loading, error, onLogin, onRegister }: LoginViewProps) {
  const [mode, setMode] = useState<AuthMode>('login')
  const [account, setAccount] = useState('')
  const [password, setPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [localError, setLocalError] = useState('')
  const [registeredHint, setRegisteredHint] = useState(false)

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault()
    setLocalError('')
    setRegisteredHint(false)

    if (!account.trim() || !password.trim()) {
      setLocalError('请输入账号和密码')
      return
    }

    if (mode === 'register' && password !== confirmPassword) {
      setLocalError('两次输入的密码不一致')
      return
    }

    if (mode === 'login') {
      await onLogin(account, password)
      return
    }

    const ok = await onRegister(account, password)
    if (ok) {
      setPassword('')
      setConfirmPassword('')
      setRegisteredHint(true)
      setMode('login')
    }
  }

  const displayError = localError || error

  return (
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] py-3">
      <form
        onSubmit={handleSubmit}
        className="w-[calc(100%-32px)] rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-5 shadow-[var(--qq-shadow)]"
      >
        <div className="mb-3 flex items-center justify-center gap-2">
          <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-[var(--qq-primary)] text-sm font-bold text-white">
            Q
          </div>
          <h1 className="text-base font-semibold text-[var(--qq-text)]">QQ NT</h1>
        </div>

        <div className="mb-3 flex rounded-lg bg-[var(--qq-bg-tertiary)] p-1">
          <button
            type="button"
            onClick={() => {
              setMode('login')
              setLocalError('')
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
              setLocalError('')
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

        <div className="space-y-2.5">
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">账号</label>
            <input
              value={account}
              onChange={(e) => setAccount(e.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
              placeholder="QQ 号 / 手机号"
              disabled={loading}
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
              disabled={loading}
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
                disabled={loading}
              />
            </div>
          ) : null}
        </div>

        {displayError ? (
          <p className="mt-2 text-center text-xs text-[var(--qq-danger)]">{displayError}</p>
        ) : null}
        {registeredHint ? (
          <p className="mt-2 text-center text-xs text-[var(--qq-success)]">注册成功，请登录</p>
        ) : null}

        <button
          type="submit"
          disabled={loading}
          className="mt-3 w-full rounded-md bg-[var(--qq-primary)] py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-60"
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