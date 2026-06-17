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

  function switchMode(nextMode: AuthMode) {
    setMode(nextMode)
    setLocalError('')
    setRegisteredHint(false)
  }

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
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] px-4 pb-5 pt-3">
      <form
        onSubmit={handleSubmit}
        className="flex h-full w-full flex-col rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] px-5 pb-4 pt-5 shadow-[var(--qq-shadow)]"
      >
        <div className="flex flex-col items-center">
          <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-[var(--qq-primary)] text-lg font-bold text-white shadow-sm">
            Q
          </div>
          <h1 className="mt-2 text-base font-semibold text-[var(--qq-text)]">QQ NT</h1>
          <p className="mt-1 text-xs text-[var(--qq-text-secondary)]">
            {mode === 'login' ? '账号密码登录' : '注册 QQ NT 账号'}
          </p>
        </div>

        <div className="mt-4 space-y-2.5">
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

        <div className="min-h-6">
          {displayError ? (
            <p className="mt-2 text-center text-xs text-[var(--qq-danger)]">{displayError}</p>
          ) : null}
          {registeredHint ? (
            <p className="mt-2 text-center text-xs text-[var(--qq-success)]">注册成功，请登录</p>
          ) : null}
        </div>

        <button
          type="submit"
          disabled={loading}
          className="mt-auto w-full rounded-md bg-[var(--qq-primary)] py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-60"
        >
          {loading
            ? mode === 'login'
              ? '登录中…'
              : '注册中…'
            : mode === 'login'
              ? '登录'
              : '注册'}
        </button>

        <div className="mt-3 flex items-center justify-center gap-2 text-xs">
          <button
            type="button"
            onClick={() => switchMode('login')}
            disabled={loading}
            className={cn(
              'transition-colors hover:text-[var(--qq-primary)] disabled:opacity-60',
              mode === 'login' ? 'font-medium text-[var(--qq-primary)]' : 'text-[var(--qq-text-secondary)]'
            )}
          >
            账号密码登录
          </button>
          <span className="text-[var(--qq-text-tertiary)]">|</span>
          <button
            type="button"
            onClick={() => switchMode('register')}
            disabled={loading}
            className={cn(
              'transition-colors hover:text-[var(--qq-primary)] disabled:opacity-60',
              mode === 'register' ? 'font-medium text-[var(--qq-primary)]' : 'text-[var(--qq-text-secondary)]'
            )}
          >
            注册账号
          </button>
        </div>
      </form>
    </div>
  )
}
