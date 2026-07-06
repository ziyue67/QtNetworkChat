import { Camera, User as UserIcon } from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import type { User } from '@/types/qqnt'

interface ProfileCardProps {
  currentUser?: User | null
  nickname: string
  signature: string
  avatar: string
  saved: boolean
  error: string
  onNicknameChange: (value: string) => void
  onSignatureChange: (value: string) => void
  onAvatarPick: () => void
  onSave: () => void
}

export function ProfileCard({
  currentUser,
  nickname,
  signature,
  avatar,
  saved,
  error,
  onNicknameChange,
  onSignatureChange,
  onAvatarPick,
  onSave
}: ProfileCardProps) {
  return (
    <div className="w-full max-w-md rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-6 shadow-[var(--qq-shadow)]">
      <h2 className="mb-6 flex items-center gap-2 text-base font-semibold text-[var(--qq-text)]">
        <UserIcon size={18} />
        个人资料
      </h2>

      <div className="mb-6 flex justify-center">
        <div className="relative">
          <Avatar src={avatar} fallback={nickname} size={96} className="h-24 w-24 text-3xl" />
          <button
            type="button"
            onClick={onAvatarPick}
            className="absolute bottom-0 right-0 flex h-8 w-8 items-center justify-center rounded-full bg-[var(--qq-bg-tertiary)] text-[var(--qq-text-secondary)] shadow-sm hover:bg-[var(--qq-border)]"
            aria-label="修改头像"
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
            onChange={(event) => onNicknameChange(event.target.value)}
            className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            aria-label="昵称"
          />
        </div>
        <div>
          <label className="mb-1 block text-xs text-[var(--qq-text-secondary)]">个性签名</label>
          <input
            value={signature}
            onChange={(event) => onSignatureChange(event.target.value)}
            placeholder="编辑个性签名"
            className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
            aria-label="个性签名"
          />
        </div>
      </div>

      <div className="mt-6 flex items-center justify-between gap-3">
        <div className="min-w-0 text-xs">
          {error ? <span className="text-[var(--qq-warning)]">{error}</span> : null}
          {!error && saved ? <span className="text-[var(--qq-success)]">已保存</span> : null}
        </div>
        <button
          type="button"
          onClick={onSave}
          disabled={!currentUser}
          className="rounded-md bg-[var(--qq-primary)] px-5 py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-50"
        >
          保存
        </button>
      </div>
    </div>
  )
}
