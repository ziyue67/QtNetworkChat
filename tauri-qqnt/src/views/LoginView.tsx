import { useEffect, useState, type FormEvent } from 'react'
import { LogicalSize } from '@tauri-apps/api/dpi'
import { invoke } from '@tauri-apps/api/core'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { open } from '@tauri-apps/plugin-dialog'
import { Upload, UserRound, X } from 'lucide-react'
import { startWindowDrag } from '@/lib/window'

const LOGIN_WINDOW_SIZE = { width: 300, height: 460 }
const REGISTER_WINDOW_SIZE = { width: 480, height: 640 }

interface ImageBase64Response {
  base64: string
  dataUrl: string
}

interface LoginViewProps {
  loading: boolean
  error?: string
  onLogin: (account: string, password: string) => Promise<void> | void
  onRegister: (
    account: string,
    password: string,
    nickname: string,
    avatarBase64?: string
  ) => Promise<boolean> | boolean
}

function useRegisterWindowSize(registerOpen: boolean) {
  useEffect(() => {
    let mounted = true

    async function resize() {
      try {
        const win = getCurrentWindow()
        const size = registerOpen ? REGISTER_WINDOW_SIZE : LOGIN_WINDOW_SIZE
        if (!mounted) return
        await win.setResizable(false)
        await win.setMinSize(new LogicalSize(size.width, size.height))
        await win.setSize(new LogicalSize(size.width, size.height))
        await win.center()
      } catch {
      }
    }

    resize()
    return () => {
      mounted = false
    }
  }, [registerOpen])
}

export function LoginView({ loading, error, onLogin, onRegister }: LoginViewProps) {
  const [account, setAccount] = useState('')
  const [password, setPassword] = useState('')
  const [localError, setLocalError] = useState('')
  const [registeredHint, setRegisteredHint] = useState(false)
  const [registerOpen, setRegisterOpen] = useState(false)
  const [registerAccount, setRegisterAccount] = useState('')
  const [registerPassword, setRegisterPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [nickname, setNickname] = useState('')
  const [avatarBase64, setAvatarBase64] = useState('')
  const [avatarPreview, setAvatarPreview] = useState('')
  const [choosingAvatar, setChoosingAvatar] = useState(false)
  const [registerError, setRegisterError] = useState('')

  useRegisterWindowSize(registerOpen)

  function openRegister() {
    setLocalError('')
    setRegisteredHint(false)
    setRegisterError('')
    setRegisterAccount(account)
    setRegisterOpen(true)
  }

  function closeRegister() {
    if (loading) return
    setRegisterOpen(false)
    setRegisterError('')
  }

  async function handleLogin(e: FormEvent) {
    e.preventDefault()
    setLocalError('')
    setRegisteredHint(false)

    if (!account.trim() || !password.trim()) {
      setLocalError('请输入账号和密码')
      return
    }

    await onLogin(account.trim(), password)
  }

  async function chooseAvatar() {
    setRegisterError('')
    setChoosingAvatar(true)

    try {
      const selected = await open({
        multiple: false,
        directory: false,
        title: '选择头像图片',
        filters: [
          {
            name: '图片',
            extensions: ['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'svg', 'avif', 'apng']
          }
        ]
      })

      if (!selected || Array.isArray(selected)) return

      const image = await invoke<ImageBase64Response>('read_image_base64', { filePath: selected })
      setAvatarBase64(image.base64)
      setAvatarPreview(image.dataUrl)
    } catch (err) {
      const message = err instanceof Error ? err.message : '无法读取头像图片，请重新选择'
      setRegisterError(message)
    } finally {
      setChoosingAvatar(false)
    }
  }

  async function handleRegister(e: FormEvent) {
    e.preventDefault()
    setRegisterError('')
    setRegisteredHint(false)

    const nextAccount = registerAccount.trim()
    const nextNickname = nickname.trim()

    if (!nextAccount || !registerPassword.trim() || !nextNickname) {
      setRegisterError('请输入账号、密码和昵称')
      return
    }

    if (registerPassword !== confirmPassword) {
      setRegisterError('两次输入的密码不一致')
      return
    }

    const ok = await onRegister(nextAccount, registerPassword, nextNickname, avatarBase64 || undefined)
    if (ok) {
      setAccount(nextAccount)
      setPassword('')
      setRegisterPassword('')
      setConfirmPassword('')
      setNickname('')
      setAvatarBase64('')
      setAvatarPreview('')
      setRegisterOpen(false)
      setRegisteredHint(true)
    }
  }

  const displayError = localError || error
  const displayRegisterError = registerError || error

  return (
    <div
      className="relative flex h-full w-full items-center justify-center overflow-hidden bg-[var(--qq-bg)] px-4 pb-5 pt-3"
      onMouseDown={startWindowDrag}
    >
      <form
        onSubmit={handleLogin}
        className="flex h-full w-full flex-col rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] px-5 pb-4 pt-5 shadow-[var(--qq-shadow)]"
      >
        <div className="flex flex-col items-center">
          <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-[var(--qq-primary)] text-lg font-bold text-white shadow-sm">
            Q
          </div>
          <h1 className="mt-2 text-base font-semibold text-[var(--qq-text)]">QQ NT</h1>
          <p className="mt-1 text-xs text-[var(--qq-text-secondary)]">账号密码登录</p>
        </div>

        <div className="mt-4 space-y-2.5">
          <div>
            <label htmlFor="qqnt-account" className="mb-1 block text-xs text-[var(--qq-text-secondary)]">账号</label>
            <input
              id="qqnt-account"
              value={account}
              onChange={(e) => setAccount(e.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
              placeholder="QQ 号 / 手机号"
              disabled={loading}
            />
          </div>
          <div>
            <label htmlFor="qqnt-password" className="mb-1 block text-xs text-[var(--qq-text-secondary)]">密码</label>
            <input
              id="qqnt-password"
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
              placeholder="请输入密码"
              disabled={loading}
            />
          </div>
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
          {loading ? '登录中…' : '登录'}
        </button>

        <div className="mt-3 flex items-center justify-center gap-2 text-xs">
          <button
            type="button"
            disabled={loading}
            className="font-medium text-[var(--qq-primary)] transition-colors disabled:opacity-60"
          >
            账号密码登录
          </button>
          <span className="text-[var(--qq-text-tertiary)]">|</span>
          <button
            type="button"
            onClick={openRegister}
            disabled={loading}
            className="text-[var(--qq-text-secondary)] transition-colors hover:text-[var(--qq-primary)] disabled:opacity-60"
          >
            注册账号
          </button>
        </div>
      </form>

      {registerOpen ? (
        <div
          role="dialog"
          aria-modal="true"
          aria-labelledby="qqnt-register-title"
          className="absolute inset-0 flex items-center justify-center bg-black/20 p-4 backdrop-blur-[1px]"
          onMouseDown={startWindowDrag}
        >
          <form
            onSubmit={handleRegister}
            className="flex h-full w-full max-w-[430px] flex-col rounded-2xl bg-white px-8 py-6 text-[#1f2329] shadow-2xl"
          >
            <div className="flex items-center justify-between">
              <div>
                <h2 id="qqnt-register-title" className="text-2xl font-semibold text-[#111827]">欢迎注册QQ</h2>
                <p className="mt-1 text-sm text-[#7b8494]">填写基础资料，头像可稍后再改</p>
              </div>
              <button
                type="button"
                aria-label="关闭注册窗口"
                onClick={closeRegister}
                disabled={loading}
                className="rounded-full p-2 text-[#8a93a3] transition-colors hover:bg-[#f2f4f7] hover:text-[#111827] disabled:opacity-60"
              >
                <X size={18} />
              </button>
            </div>

            <div className="mt-6 flex justify-center">
              <button
                type="button"
                onClick={chooseAvatar}
                disabled={loading || choosingAvatar}
                className="group flex flex-col items-center gap-2 rounded-2xl px-5 py-3 text-sm text-[#576071] transition-colors hover:bg-[#f6f8fb] disabled:opacity-60"
              >
                <span className="flex h-20 w-20 items-center justify-center overflow-hidden rounded-full bg-[#eef6ff] text-[#1687ff] ring-1 ring-[#d7eaff]">
                  {avatarPreview ? (
                    <img src={avatarPreview} alt="头像预览" className="h-full w-full object-cover" />
                  ) : (
                    <UserRound size={34} />
                  )}
                </span>
                <span className="inline-flex items-center gap-1">
                  <Upload size={14} />
                  {choosingAvatar ? '正在打开…' : '选择头像图片'}
                </span>
              </button>
            </div>

            <div className="mt-5 space-y-3">
              <div>
                <label htmlFor="qqnt-register-account" className="mb-1 block text-xs text-[#697386]">账号</label>
                <input
                  id="qqnt-register-account"
                  value={registerAccount}
                  onChange={(e) => setRegisterAccount(e.target.value)}
                  className="w-full rounded-lg border border-[#dfe4ea] bg-white px-3 py-2.5 text-sm outline-none transition-colors focus:border-[#1687ff]"
                  placeholder="QQ 号 / 手机号"
                  disabled={loading}
                />
              </div>
              <div>
                <label htmlFor="qqnt-register-nickname" className="mb-1 block text-xs text-[#697386]">昵称</label>
                <input
                  id="qqnt-register-nickname"
                  value={nickname}
                  onChange={(e) => setNickname(e.target.value)}
                  className="w-full rounded-lg border border-[#dfe4ea] bg-white px-3 py-2.5 text-sm outline-none transition-colors focus:border-[#1687ff]"
                  placeholder="请输入昵称"
                  disabled={loading}
                />
              </div>
              <div>
                <label htmlFor="qqnt-register-password" className="mb-1 block text-xs text-[#697386]">密码</label>
                <input
                  id="qqnt-register-password"
                  type="password"
                  value={registerPassword}
                  onChange={(e) => setRegisterPassword(e.target.value)}
                  className="w-full rounded-lg border border-[#dfe4ea] bg-white px-3 py-2.5 text-sm outline-none transition-colors focus:border-[#1687ff]"
                  placeholder="请输入密码"
                  disabled={loading}
                />
              </div>
              <div>
                <label htmlFor="qqnt-confirm-password" className="mb-1 block text-xs text-[#697386]">确认密码</label>
                <input
                  id="qqnt-confirm-password"
                  type="password"
                  value={confirmPassword}
                  onChange={(e) => setConfirmPassword(e.target.value)}
                  className="w-full rounded-lg border border-[#dfe4ea] bg-white px-3 py-2.5 text-sm outline-none transition-colors focus:border-[#1687ff]"
                  placeholder="请再次输入密码"
                  disabled={loading}
                />
              </div>
            </div>

            <div className="min-h-7">
              {displayRegisterError ? (
                <p className="mt-2 text-center text-xs text-[var(--qq-danger)]">{displayRegisterError}</p>
              ) : null}
            </div>

            <button
              type="submit"
              disabled={loading || choosingAvatar}
              className="mt-auto w-full rounded-lg bg-[#1687ff] py-2.5 text-sm font-medium text-white transition-colors hover:bg-[#0673e7] disabled:opacity-60"
            >
              {loading ? '注册中…' : '立即注册'}
            </button>
          </form>
        </div>
      ) : null}
    </div>
  )
}
