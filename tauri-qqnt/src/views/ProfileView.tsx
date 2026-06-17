import { useState } from 'react'
import { Camera, User } from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import { useAuthStore } from '@/stores/authStore'
import { profileUpdate } from '@/api/qqnt'

export function ProfileView() {
  const currentUser = useAuthStore((state) => state.currentUser)
  const setCurrentUser = useAuthStore((state) => state.setCurrentUser)
  const [nickname, setNickname] = useState(currentUser?.nickname || '')
  const [signature, setSignature] = useState(currentUser?.signature || '')
  const [avatar, setAvatar] = useState(currentUser?.avatar || '')
  const [saved, setSaved] = useState(false)
  const [error, setError] = useState('')

  async function handleSave() {
    if (!currentUser) return
    setError('')
    const nextUser = { ...currentUser, nickname, signature, avatar }
    setCurrentUser(nextUser)
    try {
      const ack = await profileUpdate({ userName: nickname, signature, avatarBase64: avatar })
      if (ack.status === 'error') throw new Error(ack.error?.message || '资料同步失败')
      setSaved(true)
    } catch (err) {
      setError(err instanceof Error ? err.message : '资料已保存到本地，等待引擎同步')
      setSaved(true)
    }
    window.setTimeout(() => setSaved(false), 2000)
  }

  function handleAvatarClick() {
    const nextAvatar = window.prompt('头像 URL 或 Base64', avatar)
    if (nextAvatar !== null) setAvatar(nextAvatar)
  }

  return (
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] p-6">
      <div className="w-full max-w-md rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-6 shadow-[var(--qq-shadow)]">
        <h2 className="mb-6 flex items-center gap-2 text-base font-semibold text-[var(--qq-text)]">
          <User size={18} />
          个人资料
        </h2>

        <div className="mb-6 flex justify-center">
          <div className="relative">
            <Avatar src={avatar} fallback={nickname} size={96} className="h-24 w-24 text-3xl" />
            <button
              onClick={handleAvatarClick}
              className="absolute bottom-0 right-0 flex h-8 w-8 items-center justify-center rounded-full bg-[var(--qq-bg-tertiary)] text-[var(--qq-text-secondary)] shadow-sm hover:bg-[var(--qq-border)]"
            >
              <Camera size={14} />
            </button>
          </div>
        </div>

        <div className="space-y-4">
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">账号</label>
            <div className="rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] px-3 py-2 text-sm text-[var(--qq-text-secondary)]">
              {currentUser?.id || '-'}
            </div>
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">昵称</label>
            <input
              value={nickname}
              onChange={(event) => setNickname(event.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            />
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">个性签名</label>
            <input
              value={signature}
              onChange={(event) => setSignature(event.target.value)}
              placeholder="编辑个性签名"
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            />
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">头像 URL / Base64</label>
            <input
              value={avatar}
              onChange={(event) => setAvatar(event.target.value)}
              placeholder="https://..."
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            />
          </div>
        </div>

        <div className="mt-6 flex items-center justify-between gap-3">
          <div className="min-w-0 text-xs">
            {error ? <span className="text-[var(--qq-warning)]">{error}</span> : null}
            {!error && saved ? <span className="text-[var(--qq-success)]">已保存</span> : null}
          </div>
          <button
            onClick={handleSave}
            disabled={!currentUser}
            className="rounded-md bg-[var(--qq-primary)] px-5 py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-50"
          >
            保存
          </button>
        </div>
      </div>
    </div>
  )
}
