import { useState } from 'react'
import { Camera } from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import { useAuthStore } from '@/stores/authStore'

export function ProfileView() {
  const currentUser = useAuthStore((state) => state.currentUser)
  const setCurrentUser = useAuthStore((state) => state.setCurrentUser)
  const [nickname, setNickname] = useState(currentUser?.nickname || '')
  const [signature, setSignature] = useState(currentUser?.signature || '')
  const [avatar, setAvatar] = useState(currentUser?.avatar || '')
  const [saved, setSaved] = useState(false)

  function handleSave() {
    if (currentUser) {
      setCurrentUser({ ...currentUser, nickname, signature, avatar })
      setSaved(true)
      setTimeout(() => setSaved(false), 2000)
    }
  }

  return (
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] p-6">
      <div className="w-full max-w-md rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-6 shadow-[var(--qq-shadow)]">
        <h2 className="mb-6 text-base font-semibold text-[var(--qq-text)]">个人资料</h2>

        <div className="mb-6 flex justify-center">
          <div className="relative">
            <Avatar src={avatar} fallback={nickname} size={96} className="h-24 w-24 text-3xl" />
            <button className="absolute bottom-0 right-0 flex h-8 w-8 items-center justify-center rounded-full bg-[var(--qq-bg-tertiary)] text-[var(--qq-text-secondary)] shadow-sm hover:bg-[var(--qq-border)]">
              <Camera size={14} />
            </button>
          </div>
        </div>

        <div className="space-y-4">
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">昵称</label>
            <input
              value={nickname}
              onChange={(e) => setNickname(e.target.value)}
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            />
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">个性签名</label>
            <input
              value={signature}
              onChange={(e) => setSignature(e.target.value)}
              placeholder="编辑个性签名"
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            />
          </div>
          <div>
            <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">头像 URL</label>
            <input
              value={avatar}
              onChange={(e) => setAvatar(e.target.value)}
              placeholder="https://..."
              className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            />
          </div>
        </div>

        <div className="mt-6 flex items-center justify-between">
          {saved ? (
            <span className="text-xs text-[var(--qq-success)]">已保存</span>
          ) : (
            <span />
          )}
          <button
            onClick={handleSave}
            className="rounded-md bg-[var(--qq-primary)] px-5 py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)]"
          >
            保存
          </button>
        </div>
      </div>
    </div>
  )
}
