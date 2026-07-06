import { useState, type FormEvent } from 'react'
import { invoke } from '@tauri-apps/api/core'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { open as openFileDialog } from '@tauri-apps/plugin-dialog'
import { Camera, Check, Eye, EyeOff, Lock, MessageCircle, User, UserCircle, X } from 'lucide-react'
import { cn } from '@/lib/utils'
import { register as apiRegister, connectServer, profileUpdate } from '@/api/qqnt'
import { Avatar } from '@/components/common/Avatar'
import { startWindowDrag } from '@/lib/window'
import { REGISTER_HINT_KEY } from '@/views/LoginView'
import { DEFAULT_SERVER_HOST, DEFAULT_SERVER_PORT } from '@/stores/authStore'

interface ImageBase64Response {
  base64: string
  dataUrl: string
}

export function RegisterWindow() {
  const [account, setAccount] = useState('')
  const [nickname, setNickname] = useState('')
  const [password, setPassword] = useState('')
  const [confirmPassword, setConfirmPassword] = useState('')
  const [showPassword, setShowPassword] = useState(false)
  const [showConfirm, setShowConfirm] = useState(false)
  const [avatar, setAvatar] = useState('')
  const [avatarBase64, setAvatarBase64] = useState('')
  const [agreed, setAgreed] = useState(false)
  const [choosingAvatar, setChoosingAvatar] = useState(false)
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState('')

  async function chooseAvatar() {
    setChoosingAvatar(true)
    setError('')

    try {
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

      if (typeof selected !== 'string') return

      const image = await invoke<ImageBase64Response>('read_image_base64', { filePath: selected })
      setAvatar(image.dataUrl)
      setAvatarBase64(image.base64)
    } catch (err) {
      setError(err instanceof Error ? err.message : '头像读取失败，请重新选择')
    } finally {
      setChoosingAvatar(false)
    }
  }

  async function handleSubmit(e: FormEvent) {
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
    if (!agreed) {
      setError('请阅读并同意服务协议和隐私政策')
      return
    }

    setSubmitting(true)
    try {
      const ack = await apiRegister(account.trim(), password, nickname.trim())
      if (ack.status === 'error') {
        setError(ack.error?.message || '注册失败')
        return
      }

      const requiresConnect = ack.payload?.requiresConnect ?? true
      if (requiresConnect) {
        const connected = await connectServer(DEFAULT_SERVER_HOST, DEFAULT_SERVER_PORT)
        if (connected?.status === 'ok' && avatarBase64) {
          await profileUpdate({ userName: nickname.trim(), avatarBase64 }).catch(() => undefined)
        }
      }

      if (typeof localStorage !== 'undefined') {
        localStorage.setItem(REGISTER_HINT_KEY, account.trim())
      }
      getCurrentWindow().close().catch(() => undefined)
    } catch (err) {
      setError(err instanceof Error ? err.message : '注册请求失败')
    } finally {
      setSubmitting(false)
    }
  }

  async function closeWindow() {
    await getCurrentWindow().close().catch(() => undefined)
  }

  const canSubmit =
    account.trim() && nickname.trim() && password.trim() && confirmPassword.trim() && agreed && !submitting

  return (
    <div className="flex h-full w-full flex-col overflow-hidden bg-gradient-to-br from-[var(--qq-bg-secondary)] via-[var(--qq-bg)] to-[var(--qq-bg-secondary)] text-[var(--qq-text)]">
      <header className="flex h-12 shrink-0 items-center border-b border-[var(--qq-border)] bg-[var(--qq-bg)]/80 backdrop-blur">
        <div className="flex h-full flex-1 items-center gap-2.5 px-4" onMouseDown={startWindowDrag}>
          <div className="flex h-7 w-7 items-center justify-center rounded-lg bg-[var(--qq-primary)] text-white">
            <MessageCircle size={16} />
          </div>
          <span className="text-sm font-medium text-[var(--qq-text)]">欢迎注册QQ</span>
        </div>
        <div className="flex h-full items-center px-2" data-no-drag onMouseDown={(event) => event.stopPropagation()}>
          <button
            type="button"
            onClick={closeWindow}
            className="flex h-8 w-8 items-center justify-center rounded-md text-[var(--qq-text-secondary)] transition-colors hover:bg-[var(--qq-danger)] hover:text-white"
            aria-label="关闭"
          >
            <X size={16} />
          </button>
        </div>
      </header>

      <main className="flex flex-1 flex-col items-center justify-center p-5">
        <div className="w-full max-w-sm rounded-2xl border border-[var(--qq-border)] bg-[var(--qq-bg)] p-7 shadow-[var(--qq-shadow)]">
          <div className="flex flex-col items-center">
            <div className="relative">
              <Avatar src={avatar} fallback={nickname || account || 'Q'} size={72} className="h-[72px] w-[72px] text-2xl" />
              <button
                type="button"
                onClick={chooseAvatar}
                disabled={choosingAvatar}
                className="absolute -bottom-1 -right-1 flex h-7 w-7 items-center justify-center rounded-full border-2 border-[var(--qq-bg)] bg-[var(--qq-primary)] text-white shadow-sm transition-transform hover:scale-110 disabled:opacity-60"
                aria-label="选择头像"
              >
                <Camera size={12} />
              </button>
            </div>
            <h1 className="mt-4 text-lg font-bold text-[var(--qq-text)]">创建新账号</h1>
            <p className="mt-0.5 text-xs text-[var(--qq-text-tertiary)]">填写下方信息完成注册</p>
          </div>

          <form onSubmit={handleSubmit} className="mt-6 flex flex-col gap-3.5">
            <div className="flex items-center gap-3 rounded-xl bg-[var(--qq-bg-tertiary)] px-3.5 transition-colors focus-within:bg-[var(--qq-bg-secondary)] focus-within:ring-2 focus-within:ring-[var(--qq-primary)]">
              <User size={16} className="text-[var(--qq-text-tertiary)]" />
              <input
                value={account}
                onChange={(e) => setAccount(e.target.value)}
                className={cn(
                  'h-11 w-full bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]',
                  error.includes('账号') && 'placeholder:text-[var(--qq-danger)]'
                )}
                placeholder="请输入账号 / 手机号"
                disabled={submitting}
              />
            </div>
            <div className="flex items-center gap-3 rounded-xl bg-[var(--qq-bg-tertiary)] px-3.5 transition-colors focus-within:bg-[var(--qq-bg-secondary)] focus-within:ring-2 focus-within:ring-[var(--qq-primary)]">
              <UserCircle size={16} className="text-[var(--qq-text-tertiary)]" />
              <input
                value={nickname}
                onChange={(e) => setNickname(e.target.value)}
                className={cn(
                  'h-11 w-full bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]',
                  error.includes('昵称') && 'placeholder:text-[var(--qq-danger)]'
                )}
                placeholder="请输入昵称"
                disabled={submitting}
              />
            </div>
            <div className="flex items-center gap-3 rounded-xl bg-[var(--qq-bg-tertiary)] px-3.5 transition-colors focus-within:bg-[var(--qq-bg-secondary)] focus-within:ring-2 focus-within:ring-[var(--qq-primary)]">
              <Lock size={16} className="text-[var(--qq-text-tertiary)]" />
              <input
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                type={showPassword ? 'text' : 'password'}
                className={cn(
                  'h-11 w-full bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]',
                  error.includes('密码') && 'placeholder:text-[var(--qq-danger)]'
                )}
                placeholder="请设置 QQ 密码"
                disabled={submitting}
              />
              <button
                type="button"
                onClick={() => setShowPassword((value) => !value)}
                className="text-[var(--qq-text-tertiary)] transition-colors hover:text-[var(--qq-text)]"
                aria-label={showPassword ? '隐藏密码' : '显示密码'}
              >
                {showPassword ? <EyeOff size={15} /> : <Eye size={15} />}
              </button>
            </div>
            <div className="flex items-center gap-3 rounded-xl bg-[var(--qq-bg-tertiary)] px-3.5 transition-colors focus-within:bg-[var(--qq-bg-secondary)] focus-within:ring-2 focus-within:ring-[var(--qq-primary)]">
              <Lock size={16} className="text-[var(--qq-text-tertiary)]" />
              <input
                value={confirmPassword}
                onChange={(e) => setConfirmPassword(e.target.value)}
                type={showConfirm ? 'text' : 'password'}
                className="h-11 w-full bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
                placeholder="请再次输入密码"
                disabled={submitting}
              />
              <button
                type="button"
                onClick={() => setShowConfirm((value) => !value)}
                className="text-[var(--qq-text-tertiary)] transition-colors hover:text-[var(--qq-text)]"
                aria-label={showConfirm ? '隐藏密码' : '显示密码'}
              >
                {showConfirm ? <EyeOff size={15} /> : <Eye size={15} />}
              </button>
            </div>

            <label className="mt-1 flex cursor-pointer items-start gap-2.5">
              <button
                type="button"
                onClick={() => setAgreed((value) => !value)}
                className={cn(
                  'mt-0.5 flex h-4 w-4 shrink-0 items-center justify-center rounded border transition-colors',
                  agreed
                    ? 'border-[var(--qq-primary)] bg-[var(--qq-primary)] text-white'
                    : 'border-[var(--qq-border)] bg-[var(--qq-bg)] text-transparent hover:border-[var(--qq-primary)]'
                )}
                aria-checked={agreed}
                role="checkbox"
              >
                <Check size={10} strokeWidth={3} />
              </button>
              <span className="select-none text-xs leading-relaxed text-[var(--qq-text-secondary)]">
                已阅读并同意
                <span className="mx-0.5 cursor-pointer text-[var(--qq-primary)] hover:underline">《服务协议》</span>
                和
                <span className="mx-0.5 cursor-pointer text-[var(--qq-primary)] hover:underline">《隐私政策》</span>
              </span>
            </label>

            <div className="min-h-5">
              {error ? <p className="text-xs text-[var(--qq-danger)]">{error}</p> : null}
            </div>

            <button
              type="submit"
              disabled={!canSubmit}
              className={cn(
                'mt-1 h-11 w-full rounded-xl text-base font-semibold text-white transition-all active:scale-[0.98] disabled:cursor-not-allowed disabled:opacity-60',
                canSubmit
                  ? 'bg-gradient-to-r from-[var(--qq-primary)] to-[var(--qq-primary-hover)] shadow-md shadow-[var(--qq-primary)]/20 hover:brightness-105'
                  : 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-text-tertiary)]'
              )}
            >
              {submitting ? '注册中…' : '立即注册'}
            </button>
          </form>
        </div>
      </main>
    </div>
  )
}
