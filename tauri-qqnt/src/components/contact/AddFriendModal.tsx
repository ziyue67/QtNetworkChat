import { useState } from 'react'
import { X, Search, UserPlus, Loader2 } from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import type { Contact } from '@/types/qqnt'

interface AddFriendModalProps {
  open: boolean
  onClose: () => void
  onSearch?: (keyword: string) => Promise<Contact | null>
  onAdd?: (contact: Contact) => Promise<void>
}

export function AddFriendModal({ open, onClose, onSearch, onAdd }: AddFriendModalProps) {
  const [keyword, setKeyword] = useState('')
  const [result, setResult] = useState<Contact | null>(null)
  const [loading, setLoading] = useState(false)
  const [adding, setAdding] = useState(false)
  const [error, setError] = useState('')
  const [added, setAdded] = useState(false)

  if (!open) return null

  async function handleSearch(e: React.FormEvent) {
    e.preventDefault()
    setError('')
    setResult(null)
    setAdded(false)
    if (!keyword.trim()) return
    setLoading(true)
    try {
      const found = onSearch ? await onSearch(keyword.trim()) : null
      if (!found) {
        setError('未找到用户')
      } else {
        setResult(found)
      }
    } catch (err) {
      setError(err instanceof Error ? err.message : '搜索失败')
    } finally {
      setLoading(false)
    }
  }

  async function handleAdd() {
    if (!result || !onAdd) return
    setAdding(true)
    setError('')
    try {
      await onAdd(result)
      setAdded(true)
    } catch (err) {
      setError(err instanceof Error ? err.message : '添加失败')
    } finally {
      setAdding(false)
    }
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 p-4">
      <div className="w-full max-w-sm rounded-xl bg-[var(--qq-bg)] p-5 shadow-[var(--qq-shadow)]">
        <div className="mb-4 flex items-center justify-between">
          <h3 className="text-base font-semibold text-[var(--qq-text)]">添加好友</h3>
          <button
            onClick={onClose}
            className="rounded p-1 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
          >
            <X size={16} />
          </button>
        </div>

        <form onSubmit={handleSearch} className="mb-4 flex gap-2">
          <input
            value={keyword}
            onChange={(e) => setKeyword(e.target.value)}
            placeholder="输入 QQ 号 / 昵称"
            className="flex-1 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] px-3 py-2 text-sm text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
          />
          <button
            type="submit"
            disabled={loading || !keyword.trim()}
            className="flex items-center gap-1 rounded-md bg-[var(--qq-primary)] px-3 py-2 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)] disabled:opacity-50"
          >
            {loading ? <Loader2 size={14} className="animate-spin" /> : <Search size={14} />}
            搜索
          </button>
        </form>

        {error ? <p className="mb-3 text-xs text-[var(--qq-danger)]">{error}</p> : null}

        {result ? (
          <div className="flex items-center gap-3 rounded-lg border border-[var(--qq-border)] p-3">
            <Avatar src={result.avatar} fallback={result.nickname} size={44} />
            <div className="min-w-0 flex-1">
              <p className="truncate text-sm font-medium text-[var(--qq-text)]">{result.nickname}</p>
              <p className="truncate text-xs text-[var(--qq-text-secondary)]">{result.signature || ' '}</p>
            </div>
            <button
              onClick={handleAdd}
              disabled={adding || added}
              className={cn(
                'flex items-center gap-1 rounded-md px-3 py-1.5 text-xs font-medium text-white transition-colors',
                added
                  ? 'bg-[var(--qq-success)]'
                  : 'bg-[var(--qq-primary)] hover:bg-[var(--qq-primary-hover)] disabled:opacity-50'
              )}
            >
              {adding ? <Loader2 size={12} className="animate-spin" /> : <UserPlus size={12} />}
              {added ? '已发送' : '加好友'}
            </button>
          </div>
        ) : null}
      </div>
    </div>
  )
}
