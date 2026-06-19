import { useEffect, useState, type FormEvent } from 'react'
import { LogicalSize } from '@tauri-apps/api/dpi'
import { invoke } from '@tauri-apps/api/core'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { open as openFileDialog } from '@tauri-apps/plugin-dialog'
import { X, ImagePlus } from 'lucide-react'
import { cn } from '@/lib/utils'
import { startWindowDrag } from '@/lib/window'
import { Avatar } from '@/components/common/Avatar'

interface RegisterForm {
  account: string
  password: string
  nickname: string
  avatar?: string
  avatarBase64?: string
}

interface ImageBase64Response {
  base64: string
  dataUrl: string
}

interface LoginViewProps {
  loading: boolean
  error?: string
  onLogin: (account: string, password: string) => Promise<void> | void
  onRegister: (form: RegisterForm) => Promise<boolean>
}

const LOGIN_WINDOW_SIZE = { width: 300, height: 460 }
const REGISTER_WINDOW_SIZE = { width: 480, height: 640 }

async function resizeAuthWindow(size: { width: number; height: number }) {
  try {
    const win = getCurrentWindow()
    await win.setMinSize(new LogicalSize(size.width, size.height))
    await win.setSize(new LogicalSize(size.width, size.height))
    await win.center()
  } catch {
  }
}

export function LoginView({ loading, error, onLogin, onRegister }: LoginViewProps) {
  const [account, setAccount] = useState('')
  const [password, setPassword] = useState('')
  const [localError, setLocalError] = useState('')
  const [registeredHint, setRegisteredHint] = useState(false)
  const [registerOpen, setRegisterOpen] = useState(false)

  useEffect(() => {
    void resizeAuthWindow(registerOpen ? REGISTER_WINDOW_SIZE : LOGIN_WINDOW_SIZE)
  }, [registerOpen])

  async function handleSubmit(e: FormEvent) {
    e.preventDefault()
    setLocalError('')
    setRegisteredHint(false)

    if (!account.trim() || !password.trim()) {
      setLocalError('请输入账号和密码')
      return
    }

    await onLogin(account, password)
  }

  async function handleRegister(form: RegisterForm) {
    const ok = await onRegister(form)
    if (ok) {
      setAccount(form.account)
      setPassword('')
      setRegisteredHint(true)
      setRegisterOpen(false)
    }
    return ok
  }

  const displayError = localError || error

  return (
    <div
      className="relative flex h-full w-full items-center justify-center bg-[var(--qq-bg)] px-4 pb-5 pt-3"
      onMouseDown={startWindowDrag}
    >
      <form
        onSubmit={handleSubmit}
        className="flex h-full w-full flex-col rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] px-5 pb-4 pt-5 shadow-[var(--qq-shadow)]"
      >
        <div className="flex flex-col items-center">
          <Avatar fallback={account || 'Q'} size={48} className="mb-2 h-12 w-12 text-lg" />
          <h1 className="text-base font-semibold text-[var(--qq-text)]">QQ NT</h1>
          <p className="mt-1 text-xs text-[var(--qq-text-secondary)]">账号密码登录</p>
        </div>

        <div className="mt-5 space-y-3">
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

        <div className="min-h-8">
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
            onClick={() => {
              setLocalError('')
              setRegisteredHint(false)
              setRegisterOpen(true)
            }}
            disabled={loading}
            className="text-[var(--qq-text-secondary)] transition-colors hover:text-[var(--qq-primary)] disabled:opacity-60"
          >
            注册账号
          </button>
        </div>
      </form>

      <RegisterWindow
        open={registerOpen}
        loading={loading}
        initialAccount={account}
        onClose={() => setRegisterOpen(false)}
        onRegister={handleRegister}
      />
    </div>
  )
}

function RegisterWindow({
  open,
  loading,
  initialAccount,
  onClose,
  onRegister
}: {
  open: boolean
  loading: boolean
  initialAccount: string
  onClose: () => void
  onRegister: (form: RegisterForm) => Promise<boolean>
}) {
  const [account, setAccount] = useState(initialAccount)
  const [nickname, setNickname] = useState('')
  const [password, setPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [avatar, setAvatar] = useState('')
  const [selectedAvatarBase64, setSelectedAvatarBase64] = useState('')
  const [choosingAvatar, setChoosingAvatar] = useState(false)
  const [error, setError] = useState('')

  useEffect(() => {
    if (!open) return
    setAccount(initialAccount)
    setNickname('')
    setPassword('')
    setConfirmPassword('')
    setAvatar('')
    setSelectedAvatarBase64('')
    setChoosingAvatar(false)
    setError('')
  }, [initialAccount, open])

  if (!open) return null

  async function chooseAvatar() {
    setChoosingAvatar(true)
    setError('')

    try {
      const selected = await openDialog()
      if (!selected) return

      const image = await invoke<ImageBase64Response>('read_image_base64', { filePath: selected })
      setAvatar(image.dataUrl)
      setSelectedAvatarBase64(image.base64)
    } catch (err) {
      setError(err instanceof Error ? err.message : '头像读取失败，请重新选择')
    } finally {
      setChoosingAvatar(false)
    }
  }

  async function openDialog() {
    const selected = await openFileDialog({
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

    return typeof selected === 'string' ? selected : undefined
  }

  async function submit(e: FormEvent) {
    e.preventDefault()
    setError('')
    if (!account.trim()) {
      setError('账号不能为空')
      return
    }
    if (!nickname.trim()) {
      setError('昵称不可以为空')
      return
    }
    if (!password.trim()) {
      setError('密码不能为空')
      return
    }
    if (password !== confirmPassword) {
      setError('两次输入的密码不一致')
      return
    }
    await onRegister({
      account: account.trim(),
      password,
      nickname: nickname.trim(),
      avatar: avatar || undefined,
      avatarBase64: selectedAvatarBase64 || undefined
    })
  }

  return (
    <div className="absolute inset-0 z-20 flex items-center justify-center bg-white">
      <form
        onSubmit={submit}
        className="flex h-full w-full max-w-[480px] flex-col bg-white px-14 pb-10 pt-5 text-[var(--qq-text)]"
      >
        <div className="flex cursor-move justify-end" onMouseDown={startWindowDrag} data-tauri-drag-region>
          <button
            type="button"
            aria-label="关闭注册"
            onClick={onClose}
            className="rounded p-1 text-black hover:bg-black/5"
          >
            <X size={16} />
          </button>
        </div>

        <h1 className="mt-7 text-3xl font-black text-black">欢迎注册QQ</h1>

        <div className="mt-8 flex flex-col items-center gap-2">
          <Avatar src={avatar} fallback={nickname || account || 'Q'} size={56} className="h-14 w-14 text-xl" />
          <button
            type="button"
            onClick={chooseAvatar}
            disabled={loading || choosingAvatar}
            className="inline-flex items-center gap-1 rounded-full bg-[#dff2ff] px-3 py-1 text-xs font-medium text-[#1296db] disabled:opacity-60"
          >
            <ImagePlus size={12} /> {choosingAvatar ? '正在打开…' : '选择头像图片'}
          </button>
        </div>

        <div className="mt-6 space-y-4" data-no-drag>
          <input
            value={account}
            onChange={(e) => setAccount(e.target.value)}
            className={cn('h-12 w-full rounded-xl border-0 bg-[#f5f5f5] px-4 text-sm text-black outline-none focus:ring-2 focus:ring-[#1296db]', error.includes('账号') && 'ring-2 ring-[var(--qq-danger)]')}
            placeholder="请输入账号 / 手机号"
            disabled={loading}
          />
          <input
            value={nickname}
            onChange={(e) => setNickname(e.target.value)}
            className={cn('h-12 w-full rounded-xl border-0 bg-[#f5f5f5] px-4 text-sm text-black outline-none focus:ring-2 focus:ring-[#1296db]', error.includes('昵称') && 'ring-2 ring-[var(--qq-danger)]')}
            placeholder="请输入昵称"
            disabled={loading}
          />
          <input
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            type="password"
            className={cn('h-12 w-full rounded-xl border-0 bg-[#f5f5f5] px-4 text-sm text-black outline-none focus:ring-2 focus:ring-[#1296db]', error.includes('密码') && 'ring-2 ring-[var(--qq-danger)]')}
            placeholder="请设置 QQ 密码"
            disabled={loading}
          />
          <input
            value={confirmPassword}
            onChange={(e) => setConfirmPassword(e.target.value)}
            type="password"
            className="h-12 w-full rounded-xl border-0 bg-[#f5f5f5] px-4 text-sm text-black outline-none focus:ring-2 focus:ring-[#1296db]"
            placeholder="请再次输入密码"
            disabled={loading}
          />
        </div>

        <div className="min-h-8">
          {error ? <p className="mt-2 text-xs text-[var(--qq-danger)]">ⓘ {error}</p> : null}
        </div>

        <button
          type="submit"
          disabled={loading}
          className="mt-auto h-12 w-full rounded-xl bg-[#aee0ff] text-base font-medium text-white hover:bg-[#8fd3ff] disabled:opacity-60"
        >
          {loading ? '注册中…' : '立即注册'}
        </button>
      </form>
    </div>
  )
}
