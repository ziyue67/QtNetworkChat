import { useEffect, useState, type FormEvent } from 'react'
import { Check } from 'lucide-react'
import { cn } from '@/lib/utils'
import { Avatar } from '@/components/common/Avatar'
import { startWindowDrag } from '@/lib/window'

interface LoginViewProps {
  loading: boolean
  error?: string
  onLogin: (account: string, password: string) => Promise<void> | void
}

export const REGISTER_HINT_KEY = 'qqnt:last-registered-account'

async function openRegisterWindow() {
  try {
    const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
    new WebviewWindow('register-account', {
      url: 'index.html#/register',
      title: '注册账号',
      width: 480,
      height: 680,
      minWidth: 400,
      minHeight: 560,
      center: true,
      resizable: true,
      decorations: false,
      visible: true
    })
  } catch {
    window.open('#/register', '_blank', 'width=480,height=680')
  }
}

function Checkbox({
  checked,
  onChange,
  label,
  disabled
}: {
  checked: boolean
  onChange: (value: boolean) => void
  label: React.ReactNode
  disabled?: boolean
}) {
  return (
    <label className={cn('flex cursor-pointer items-center gap-2', disabled && 'cursor-not-allowed opacity-60')}>
      <button
        type="button"
        disabled={disabled}
        onClick={() => onChange(!checked)}
        className={cn(
          'flex h-4 w-4 items-center justify-center rounded-full border transition-colors',
          checked
            ? 'border-[#1296db] bg-[#1296db] text-white'
            : 'border-[var(--qq-border)] bg-white text-transparent hover:border-[#1296db]'
        )}
        aria-checked={checked}
        role="checkbox"
      >
        <Check size={10} strokeWidth={3} />
      </button>
      <span className="select-none text-xs text-[var(--qq-text-secondary)]">{label}</span>
    </label>
  )
}

export function LoginView({ loading, error, onLogin }: LoginViewProps) {
  const [account, setAccount] = useState('')
  const [password, setPassword] = useState('')
  const [autoLogin, setAutoLogin] = useState(true)
  const [rememberPassword, setRememberPassword] = useState(true)
  const [agreed, setAgreed] = useState(false)
  const [localError, setLocalError] = useState('')
  const [registeredHint, setRegisteredHint] = useState(false)

  useEffect(() => {
    if (typeof localStorage === 'undefined') return
    const lastAccount = localStorage.getItem(REGISTER_HINT_KEY)
    if (lastAccount) {
      setAccount(lastAccount)
      setPassword('')
      setRegisteredHint(true)
      localStorage.removeItem(REGISTER_HINT_KEY)
    }
  }, [])

  async function handleSubmit(e: FormEvent) {
    e.preventDefault()
    setLocalError('')
    setRegisteredHint(false)

    if (!account.trim() || !password.trim()) {
      setLocalError('请输入账号和密码')
      return
    }
    if (!agreed) {
      setLocalError('请阅读并同意服务协议和隐私政策')
      return
    }

    await onLogin(account, password)
  }

  const displayError = localError || error
  const canSubmit = account.trim() && password.trim() && agreed && !loading

  return (
    <div
      className="relative flex h-full w-full select-none flex-col bg-gradient-to-b from-[#eaf6ff] via-[#f8fbff] to-[#f0f4ff] px-8 pb-5 pt-3"
      onMouseDown={startWindowDrag}
    >
      <div className="flex flex-1 flex-col items-center justify-center">
        <h1 className="bg-gradient-to-r from-[#1296db] to-[#a78bfa] bg-clip-text text-4xl font-extrabold text-transparent">
          QQ
        </h1>

        <div className="mt-6 flex flex-col items-center">
          <div className="rounded-full p-1 shadow-lg shadow-[#1296db]/10">
            <Avatar
              fallback={account || 'Q'}
              size={88}
              className="h-[88px] w-[88px] border-4 border-white text-3xl"
            />
          </div>
          {registeredHint ? (
            <p className="mt-3 text-xs text-[var(--qq-success)]">注册成功，请登录</p>
          ) : null}
        </div>

        <form
          onSubmit={handleSubmit}
          className="mt-8 flex w-full flex-col gap-4"
          data-no-drag
          onMouseDown={(e) => e.stopPropagation()}
        >
          <input
            value={account}
            onChange={(e) => setAccount(e.target.value)}
            className="h-12 w-full rounded-2xl bg-white px-5 text-center text-base text-[var(--qq-text)] shadow-sm outline-none placeholder:text-[var(--qq-text-tertiary)] focus:ring-2 focus:ring-[#1296db]"
            placeholder="请输入账号 / 手机号"
            disabled={loading}
          />
          <input
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            type="password"
            className="h-12 w-full rounded-2xl bg-white px-5 text-center text-base text-[var(--qq-text)] shadow-sm outline-none placeholder:text-[var(--qq-text-tertiary)] focus:ring-2 focus:ring-[#1296db]"
            placeholder="请输入密码"
            disabled={loading}
          />

          <div className="flex items-center justify-between px-1">
            <Checkbox checked={autoLogin} onChange={setAutoLogin} label="自动登录" />
            <Checkbox checked={rememberPassword} onChange={setRememberPassword} label="记住密码" />
          </div>

          {displayError ? (
            <p className="text-center text-xs text-[var(--qq-danger)]">{displayError}</p>
          ) : null}

          <button
            type="submit"
            disabled={!canSubmit}
            className={cn(
              'mt-1 h-12 w-full rounded-2xl text-base font-semibold text-white transition-all active:scale-[0.98] disabled:cursor-not-allowed disabled:opacity-80',
              canSubmit
                ? 'bg-[#1296db] shadow-md shadow-[#1296db]/25 hover:bg-[#1089c8]'
                : 'bg-[#aee0ff]'
            )}
          >
            {loading ? '登录中…' : '登 录'}
          </button>

          <label className="mx-auto flex cursor-pointer items-center gap-2">
            <button
              type="button"
              onClick={() => setAgreed((value) => !value)}
              className={cn(
                'mt-0.5 flex h-4 w-4 items-center justify-center rounded-full border transition-colors',
                agreed
                  ? 'border-[#1296db] bg-[#1296db] text-white'
                  : 'border-[var(--qq-border)] bg-white text-transparent hover:border-[#1296db]'
              )}
              aria-checked={agreed}
              role="checkbox"
            >
              <Check size={10} strokeWidth={3} />
            </button>
            <span className="select-none text-xs text-[var(--qq-text-secondary)]">
              已阅读并同意
              <span className="mx-0.5 cursor-pointer text-[#1296db] hover:underline">服务协议</span>
              和
              <span className="mx-0.5 cursor-pointer text-[#1296db] hover:underline">隐私政策</span>
            </span>
          </label>
        </form>
      </div>

      <div className="flex items-center justify-center gap-2 pb-2 text-xs text-[var(--qq-text-secondary)]">
        <button
          type="button"
          disabled={loading}
          onClick={openRegisterWindow}
          className="font-medium text-[#1296db] transition-colors hover:text-[#1089c8] disabled:opacity-60"
        >
          注册账号
        </button>
      </div>
    </div>
  )
}
