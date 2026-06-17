import { useState, useMemo } from 'react'
import { X, Users, Loader2, Check } from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import { useContactStore } from '@/stores/contactStore'


interface CreateGroupModalProps {
  open: boolean
  onClose: () => void
  onCreate?: (name: string, members: string[]) => Promise<void>
}

export function CreateGroupModal({ open, onClose, onCreate }: CreateGroupModalProps) {
  const [name, setName] = useState('')
  const [selected, setSelected] = useState<Set<string>>(new Set())
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  const contacts = useContactStore((state) => state.contacts)
  const friends = useMemo(() => contacts.filter((c) => !c.id.startsWith('g-')), [contacts])

  if (!open) return null

  function toggle(id: string) {
    setSelected((prev) => {
      const next = new Set(prev)
      if (next.has(id)) next.delete(id)
      else next.add(id)
      return next
    })
  }

  async function handleCreate() {
    setError('')
    if (!name.trim()) {
      setError('请输入群名称')
      return
    }
    if (selected.size === 0) {
      setError('请至少选择一名成员')
      return
    }
    setLoading(true)
    try {
      await onCreate?.(name.trim(), Array.from(selected))
      setName('')
      setSelected(new Set())
      onClose()
    } catch (err) {
      setError(err instanceof Error ? err.message : '创建失败')
    } finally {
      setLoading(false)
    }
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 p-4">
      <div className="flex h-[520px] w-full max-w-md flex-col rounded-xl bg-[var(--qq-bg)] shadow-[var(--qq-shadow)]">
        <div className="flex items-center justify-between border-b border-[var(--qq-border)] px-4 py-3">
          <h3 className="text-base font-semibold text-[var(--qq-text)]">创建群聊</h3>
          <button
            onClick={onClose}
            className="rounded p-1 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
          >
            <X size={16} />
          </button>
        </div>

        <div className="px-4 py-3">
          <input
            value={name}
            onChange={(e) => setName(e.target.value)}
            placeholder="群名称"
            className="w-full rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
          />
        </div>

        <div className="flex-1 overflow-y-auto px-2">
          <p className="px-2 py-2 text-xs text-[var(--qq-text-tertiary)]">选择成员</p>
          {friends.map((friend) => {
            const isSelected = selected.has(friend.id)
            return (
              <button
                key={friend.id}
                onClick={() => toggle(friend.id)}
                className={cn(
                  'flex w-full items-center gap-3 rounded-lg px-2 py-2 text-left transition-colors',
                  isSelected ? 'bg-[var(--qq-primary-soft)]' : 'hover:bg-[var(--qq-bg-tertiary)]'
                )}
              >
                <div
                  className={cn(
                    'flex h-5 w-5 items-center justify-center rounded border transition-colors',
                    isSelected
                      ? 'border-[var(--qq-primary)] bg-[var(--qq-primary)] text-white'
                      : 'border-[var(--qq-border)]'
                  )}
                >
                  {isSelected ? <Check size={12} /> : null}
                </div>
                <Avatar src={friend.avatar} fallback={friend.nickname} size={36} />
                <span className="min-w-0 flex-1 truncate text-sm text-[var(--qq-text)]">{friend.nickname}</span>
              </button>
            )
          })}
          {friends.length === 0 ? (
            <p className="px-2 py-4 text-center text-xs text-[var(--qq-text-secondary)]">暂无好友</p>
          ) : null}
        </div>

        <div className="border-t border-[var(--qq-border)] p-4">
          {error ? <p className="mb-2 text-xs text-[var(--qq-danger)]">{error}</p> : null}
          <button
            onClick={handleCreate}
            disabled={loading}
            className="flex w-full items-center justify-center gap-2 rounded-md bg-[var(--qq-primary)] py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-50"
          >
            {loading ? <Loader2 size={14} className="animate-spin" /> : <Users size={14} />}
            创建（{selected.size}）
          </button>
        </div>
      </div>
    </div>
  )
}
