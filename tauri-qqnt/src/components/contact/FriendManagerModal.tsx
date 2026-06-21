import { useEffect, useMemo, useState } from 'react'
import { Plus, Search, X } from 'lucide-react'
import {
  ALL_FRIENDS_GROUP,
  DEFAULT_CONTACT_GROUP,
  getContactGroups,
  getContactsByGroup,
  useContactStore
} from '@/stores/contactStore'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import type { Contact } from '@/types/qqnt'

interface FriendManagerModalProps {
  open: boolean
  onClose: () => void
}

function matchesManagerQuery(contact: Contact, q: string): boolean {
  const query = q.trim().toLowerCase()
  if (!query) return true
  return (
    contact.nickname.toLowerCase().includes(query) ||
    contact.id.toLowerCase().includes(query) ||
    (!!contact.remark && contact.remark.toLowerCase().includes(query)) ||
    (!!contact.group && contact.group.toLowerCase().includes(query))
  )
}

function permissionText(contact: Contact): string {
  return contact.status === 'offline' ? '离线可见' : '正常'
}

export function FriendManagerModal({ open, onClose }: FriendManagerModalProps) {
  const contacts = useContactStore((state) => state.contacts)
  const [query, setQuery] = useState('')
  const [selectedGroup, setSelectedGroup] = useState(ALL_FRIENDS_GROUP)
  const [selectedIds, setSelectedIds] = useState<Set<string>>(new Set())

  useEffect(() => {
    if (!open) {
      setQuery('')
      setSelectedGroup(ALL_FRIENDS_GROUP)
      setSelectedIds(new Set())
    }
  }, [open])

  useEffect(() => {
    if (!open) return
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') onClose()
    }
    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
  }, [open, onClose])

  const groupInfos = useMemo(() => getContactGroups(useContactStore.getState()), [contacts])
  const filteredContacts = useMemo(() => {
    return getContactsByGroup(contacts, selectedGroup).filter((contact) => matchesManagerQuery(contact, query))
  }, [contacts, selectedGroup, query])

  const visibleIds = useMemo(() => filteredContacts.map((contact) => contact.id), [filteredContacts])
  const allVisibleSelected = visibleIds.length > 0 && visibleIds.every((id) => selectedIds.has(id))

  function toggleOne(id: string) {
    setSelectedIds((prev) => {
      const next = new Set(prev)
      if (next.has(id)) next.delete(id)
      else next.add(id)
      return next
    })
  }

  function toggleAll() {
    setSelectedIds((prev) => {
      const next = new Set(prev)
      if (allVisibleSelected) {
        for (const id of visibleIds) next.delete(id)
      } else {
        for (const id of visibleIds) next.add(id)
      }
      return next
    })
  }

  if (!open) return null

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 p-4"
      onClick={(event) => {
        if (event.target === event.currentTarget) onClose()
      }}
    >
      <div className="relative flex h-[600px] w-[840px] overflow-hidden rounded-lg bg-[var(--qq-bg)] shadow-[var(--qq-shadow)]">
        <aside className="flex w-[200px] flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] p-2">
          <button
            onClick={() => setSelectedGroup(ALL_FRIENDS_GROUP)}
            className={cn(
              'flex items-center justify-between rounded-md px-3 py-2 text-left text-sm transition-colors',
              selectedGroup === ALL_FRIENDS_GROUP
                ? 'bg-[var(--qq-bg-tertiary)] font-medium text-[var(--qq-text)]'
                : 'text-[var(--qq-text)] hover:bg-[var(--qq-bg-tertiary)]'
            )}
          >
            <span>{ALL_FRIENDS_GROUP}</span>
            <span className="text-xs text-[var(--qq-text-tertiary)]">{contacts.length}</span>
          </button>

          <div className="px-3 pb-1 pt-3 text-xs text-[var(--qq-text-tertiary)]">分组</div>
          <div className="min-h-0 flex-1 overflow-y-auto">
            {groupInfos.map(({ name, count }) => (
              <button
                key={name}
                onClick={() => setSelectedGroup(name)}
                className={cn(
                  'flex w-full items-center justify-between rounded-md px-3 py-2 text-left text-sm transition-colors',
                  selectedGroup === name
                    ? 'bg-[var(--qq-bg-tertiary)] font-medium text-[var(--qq-text)]'
                    : 'text-[var(--qq-text)] hover:bg-[var(--qq-bg-tertiary)]'
                )}
              >
                <span className="truncate">{name}</span>
                <span className="ml-2 text-xs text-[var(--qq-text-tertiary)]">{count}</span>
              </button>
            ))}
          </div>

          <button
            disabled
            title="即将上线"
            className="mt-2 flex h-8 items-center justify-center gap-1.5 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] text-sm text-[var(--qq-text-secondary)] opacity-80"
          >
            <Plus size={14} />
            添加分组
          </button>
        </aside>

        <main className="flex min-w-0 flex-1 flex-col bg-[var(--qq-bg)]">
          <div className="flex items-center justify-between px-8 py-8 pb-5">
            <h3 className="text-xl font-semibold text-[var(--qq-text)]">好友管理器</h3>
            <div className="flex h-8 w-[200px] items-center gap-2 rounded-md bg-[var(--qq-bg-secondary)] px-3">
              <Search size={14} className="text-[var(--qq-text-tertiary)]" />
              <input
                autoFocus
                value={query}
                onChange={(event) => setQuery(event.target.value)}
                placeholder="搜索"
                className="min-w-0 flex-1 bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
              />
            </div>
            <button
              onClick={onClose}
              className="absolute right-5 top-4 rounded p-1 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
              aria-label="关闭"
            >
              <X size={16} />
            </button>
          </div>

          <div className="mx-5 grid grid-cols-[36px_1.3fr_1fr_1fr_1fr] items-center border-b border-[var(--qq-border)] px-3 py-2 text-sm font-medium text-[var(--qq-text)]">
            <input type="checkbox" checked={allVisibleSelected} onChange={toggleAll} aria-label="全选" />
            <span>昵称</span>
            <span>备注</span>
            <span>分组</span>
            <span className="text-right">好友权限</span>
          </div>

          <div className="min-h-0 flex-1 overflow-y-auto px-5 pb-4">
            {filteredContacts.length === 0 ? (
              <div className="flex h-full items-center justify-center text-sm text-[var(--qq-text-tertiary)]">
                暂无好友
              </div>
            ) : (
              filteredContacts.map((contact) => {
                const checked = selectedIds.has(contact.id)
                const groupName = contact.group || DEFAULT_CONTACT_GROUP
                return (
                  <div
                    key={contact.id}
                    className={cn(
                      'grid grid-cols-[36px_1.3fr_1fr_1fr_1fr] items-center border-b border-[var(--qq-border)] px-3 py-2.5 text-sm transition-colors',
                      checked ? 'bg-[var(--qq-primary-soft)]' : 'hover:bg-[var(--qq-bg-secondary)]'
                    )}
                  >
                    <input
                      type="checkbox"
                      checked={checked}
                      onChange={() => toggleOne(contact.id)}
                      aria-label={`选择 ${contact.nickname}`}
                    />
                    <div className="flex min-w-0 items-center gap-2">
                      <Avatar src={contact.avatar} fallback={contact.nickname} size={28} />
                      <span className="truncate text-[var(--qq-text)]">{contact.nickname}</span>
                    </div>
                    <span className="truncate text-[var(--qq-text)]">{contact.remark || '-'}</span>
                    <select
                      value={groupName}
                      disabled
                      className="w-[92px] rounded bg-transparent text-[var(--qq-text)] outline-none disabled:opacity-100"
                    >
                      <option>{groupName}</option>
                    </select>
                    <span className="truncate text-right text-[var(--qq-text-secondary)]">{permissionText(contact)}</span>
                  </div>
                )
              })
            )}
          </div>
        </main>
      </div>
    </div>
  )
}

