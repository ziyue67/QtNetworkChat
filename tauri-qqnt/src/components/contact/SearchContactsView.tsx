import { useState, useMemo } from 'react'
import { SearchBar } from '@/components/session/SearchBar'
import { useContactStore } from '@/stores/contactStore'
import type { Contact } from '@/types/qqnt'

interface SearchContactsViewProps {
  onSelect?: (contact: Contact) => void
  excludeIds?: string[]
  className?: string
}

export function SearchContactsView({ onSelect, excludeIds, className }: SearchContactsViewProps) {
  const contacts = useContactStore((state) => state.contacts)
  const groups = useContactStore((state) => state.groups)
  const [query, setQuery] = useState('')

  const filtered = useMemo(() => {
    const all = [...contacts, ...groups]
    const excluded = new Set(excludeIds || [])
    if (!query.trim()) return all.filter((c) => !excluded.has(c.id))
    const q = query.trim().toLowerCase()
    return all.filter(
      (c) => !excluded.has(c.id) && (c.nickname.toLowerCase().includes(q) || c.id.toLowerCase().includes(q))
    )
  }, [contacts, groups, query, excludeIds])

  return (
    <div className={className}>
      <div className="border-b border-[var(--qq-border)] p-3">
        <SearchBar value={query} onChange={setQuery} placeholder="搜索联系人 / 群聊" />
      </div>
      <div className="max-h-64 overflow-y-auto py-1">
        {filtered.map((contact) => (
          <button
            key={contact.id}
            onClick={() => onSelect?.(contact)}
            className="flex w-full items-center gap-3 px-4 py-2 text-left transition-colors hover:bg-[var(--qq-bg-tertiary)]"
          >
            <div className="flex h-9 w-9 shrink-0 items-center justify-center rounded-full bg-[var(--qq-primary-soft)] text-sm font-medium text-[var(--qq-primary)]">
              {contact.nickname[0]?.toUpperCase() || '?'}
            </div>
            <div className="min-w-0 flex-1">
              <p className="truncate text-sm text-[var(--qq-text)]">{contact.nickname}</p>
              <p className="truncate text-xs text-[var(--qq-text-secondary)]">{contact.id}</p>
            </div>
          </button>
        ))}
        {filtered.length === 0 ? (
          <p className="px-4 py-4 text-center text-xs text-[var(--qq-text-secondary)]">无匹配结果</p>
        ) : null}
      </div>
    </div>
  )
}
