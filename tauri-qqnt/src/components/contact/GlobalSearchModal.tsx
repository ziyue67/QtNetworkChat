import { useEffect, useMemo, useState } from 'react'
import { Bot, ChevronRight, Search, X } from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import { useContactStore } from '@/stores/contactStore'
import { cn } from '@/lib/utils'
import type { Contact } from '@/types/qqnt'

interface GlobalSearchModalProps {
  open: boolean
  onClose: () => void
  initialTab?: SearchTab
}

type SearchTab = 'all' | 'users' | 'groups' | 'miniprograms' | 'bots'

const TABS: { key: SearchTab; label: string }[] = [
  { key: 'all', label: '全部' },
  { key: 'users', label: '用户' },
  { key: 'groups', label: '群聊' },
  { key: 'miniprograms', label: '小程序' },
  { key: 'bots', label: '机器人' }
]

function matchesQueryText(query: string, ...values: Array<string | undefined>): boolean {
  const q = query.trim().toLowerCase()
  if (!q) return true
  return values.some((value) => value?.toLowerCase().includes(q))
}

function contactMatches(contact: Contact, query: string): boolean {
  return matchesQueryText(query, contact.nickname, contact.id, contact.remark, contact.signature, contact.category, ...(contact.tags ?? []))
}

export function GlobalSearchModal({ open, onClose, initialTab = 'all' }: GlobalSearchModalProps) {
  const [query, setQuery] = useState('')
  const [activeTab, setActiveTab] = useState<SearchTab>(initialTab)
  const [activeCategory, setActiveCategory] = useState('全部')
  const [selectedGroup, setSelectedGroup] = useState<Contact | null>(null)
  const [toast, setToast] = useState<string | null>(null)
  const contacts = useContactStore((state) => state.contacts)
  const groups = useContactStore((state) => state.groups)

  useEffect(() => {
    if (!open) {
      setQuery('')
      setActiveTab(initialTab)
      setActiveCategory('全部')
      setSelectedGroup(null)
      setToast(null)
    }
  }, [open, initialTab])

  useEffect(() => {
    if (!open) return
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') onClose()
    }
    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
  }, [open, onClose])

  useEffect(() => {
    if (!toast) return
    const timer = window.setTimeout(() => setToast(null), 1600)
    return () => window.clearTimeout(timer)
  }, [toast])

  const filteredUsers = useMemo(() => contacts.filter((contact) => contactMatches(contact, query)), [contacts, query])
  const groupCategories = useMemo(() => {
    const categories = groups.map((group) => group.category).filter((category): category is string => Boolean(category))
    return Array.from(new Set(categories))
  }, [groups])
  const localGroups = useMemo(() => {
    return groups.filter((group) => {
      const matchesCategory = activeCategory === '全部' || group.category === activeCategory
      return matchesCategory && contactMatches(group, query)
    })
  }, [groups, activeCategory, query])

  function handleJoinGroup() {
    setToast('加群申请已发送')
  }

  if (!open) return null

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 p-4"
      onClick={(event) => {
        if (event.target === event.currentTarget) onClose()
      }}
    >
      <div className="relative flex h-[560px] w-[660px] flex-col overflow-hidden rounded-lg bg-[var(--qq-bg)] shadow-[var(--qq-shadow)]">
        <div className="flex h-9 items-center justify-center border-b border-[var(--qq-border)] text-sm font-medium text-[var(--qq-text)]">
          综合搜索
          <button
            onClick={onClose}
            className="absolute right-3 top-2 rounded p-1 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
            aria-label="关闭"
          >
            <X size={15} />
          </button>
        </div>

        <div className="flex items-center gap-2 px-2 py-2">
          <div className="flex h-9 min-w-0 flex-1 items-center gap-2 rounded-md bg-[var(--qq-bg-secondary)] px-3">
            <Search size={14} className="text-[var(--qq-text-tertiary)]" />
            <input
              autoFocus
              value={query}
              onChange={(event) => setQuery(event.target.value)}
              placeholder="输入搜索关键词"
              className="min-w-0 flex-1 bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
            />
          </div>
          <button
            onClick={() => setToast(query.trim() ? '已更新搜索结果' : '请输入搜索关键词')}
            className="h-9 rounded-lg bg-[var(--qq-primary)] px-6 text-sm font-medium text-white hover:bg-[var(--qq-primary-hover)]"
          >
            搜索
          </button>
        </div>

        <div className="flex border-b border-[var(--qq-border)] px-2">
          {TABS.map((tab) => (
            <button
              key={tab.key}
              onClick={() => setActiveTab(tab.key)}
              className={cn(
                'relative px-4 py-2 text-sm transition-colors',
                activeTab === tab.key ? 'text-[var(--qq-primary)]' : 'text-[var(--qq-text)] hover:text-[var(--qq-primary)]'
              )}
            >
              {tab.label}
              {activeTab === tab.key ? (
                <span className="absolute bottom-0 left-3 right-3 h-0.5 rounded-full bg-[var(--qq-primary)]" />
              ) : null}
            </button>
          ))}
        </div>

        <div className="min-h-0 flex-1 overflow-y-auto bg-[var(--qq-bg-secondary)] p-2">
          {activeTab === 'miniprograms' || activeTab === 'bots' ? (
            <Placeholder type={activeTab === 'miniprograms' ? '小程序' : '机器人'} />
          ) : (
            <div className="rounded-lg bg-[var(--qq-bg)] p-2">
              {(activeTab === 'all' || activeTab === 'groups') && query.trim() === '' && groupCategories.length > 0 ? (
                <div className="mb-3 flex flex-wrap gap-1.5">
                  {['全部', ...groupCategories].map((category) => (
                    <button
                      key={category}
                      onClick={() => setActiveCategory(category)}
                      className={cn(
                        'rounded-md border px-3 py-1 text-xs transition-colors',
                        activeCategory === category
                          ? 'border-[var(--qq-primary)] bg-[var(--qq-primary-soft)] text-[var(--qq-primary)]'
                          : 'border-[var(--qq-border)] text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-secondary)]'
                      )}
                    >
                      {category}
                    </button>
                  ))}
                </div>
              ) : null}

              {activeTab === 'users' ? (
                <UserResultList items={filteredUsers} />
              ) : activeTab === 'groups' ? (
                <GroupResults groups={localGroups} onJoin={handleJoinGroup} onOpenDetail={setSelectedGroup} />
              ) : (
                <div className="space-y-5">
                  {query.trim() === '' ? (
                    <GroupResults groups={localGroups} onJoin={handleJoinGroup} onOpenDetail={setSelectedGroup} />
                  ) : (
                    <>
                      <ResultHeading label="用户" count={filteredUsers.length} />
                      <UserResultList items={filteredUsers} />
                      <ResultHeading label="群聊" count={localGroups.length} />
                      <GroupResults groups={localGroups} onJoin={handleJoinGroup} onOpenDetail={setSelectedGroup} />
                    </>
                  )}
                </div>
              )}
            </div>
          )}
        </div>

        {toast ? (
          <div className="absolute bottom-5 left-1/2 -translate-x-1/2 rounded-full bg-[var(--qq-text)] px-4 py-1.5 text-xs text-white shadow-lg">
            {toast}
          </div>
        ) : null}

        {selectedGroup ? (
          <GroupDetailPanel group={selectedGroup} onClose={() => setSelectedGroup(null)} onJoin={handleJoinGroup} />
        ) : null}
      </div>
    </div>
  )
}

function ResultHeading({ label, count }: { label: string; count: number }) {
  return <h4 className="mb-2 text-xs text-[var(--qq-text-secondary)]">{label}{count ? `（${count}）` : ''}</h4>
}

function Placeholder({ type }: { type: string }) {
  return (
    <div className="flex h-full min-h-[360px] flex-col items-center justify-center gap-2 text-sm text-[var(--qq-text-secondary)]">
      <Bot size={38} className="text-[var(--qq-text-tertiary)]" />
      <p>{type}功能即将上线</p>
    </div>
  )
}

function UserResultList({ items }: { items: Contact[] }) {
  if (items.length === 0) {
    return <p className="py-8 text-center text-sm text-[var(--qq-text-tertiary)]">未找到匹配用户</p>
  }
  return (
    <div className="space-y-1">
      {items.map((user) => (
        <div key={user.id} className="flex items-center gap-3 rounded-md px-2 py-2 hover:bg-[var(--qq-bg-secondary)]">
          <Avatar src={user.avatar} fallback={user.nickname} size={36} />
          <div className="min-w-0 flex-1">
            <p className="truncate text-sm font-medium text-[var(--qq-text)]">{user.nickname}</p>
            <p className="truncate text-xs text-[var(--qq-text-secondary)]">{user.remark || user.id}</p>
          </div>
        </div>
      ))}
    </div>
  )
}

function GroupResults({
  groups,
  onJoin,
  onOpenDetail
}: {
  groups: Contact[]
  onJoin: () => void
  onOpenDetail: (group: Contact) => void
}) {
  if (groups.length === 0) {
    return <p className="py-8 text-center text-sm text-[var(--qq-text-tertiary)]">未找到匹配群聊</p>
  }
  return (
    <div className="space-y-1">
      {groups.map((group) => (
        <LocalGroupRow key={group.id} group={group} onJoin={onJoin} onOpenDetail={onOpenDetail} />
      ))}
    </div>
  )
}

function LocalGroupRow({
  group,
  onJoin,
  onOpenDetail
}: {
  group: Contact
  onJoin: () => void
  onOpenDetail: (group: Contact) => void
}) {
  return (
    <div className="flex items-center gap-3 rounded-md px-2 py-2 hover:bg-[var(--qq-bg-secondary)]">
      <button type="button" onClick={() => onOpenDetail(group)} aria-label={`查看 ${group.nickname} 详情`}> 
        <Avatar src={group.avatar} fallback={group.nickname} size={42} />
      </button>
      <div className="min-w-0 flex-1">
        <button type="button" onClick={() => onOpenDetail(group)} className="block max-w-full truncate text-left text-sm font-semibold text-[var(--qq-text)] hover:text-[var(--qq-primary)]">{group.nickname}</button>
        <p className="mt-1 truncate text-xs text-[var(--qq-text-secondary)]">
          {group.memberCount ? `${group.memberCount} 人` : '0 人'} {group.category ? ` · ${group.category}` : ''}
        </p>
        {group.tags?.length ? (
          <div className="mt-1 flex flex-wrap gap-1">
            {group.tags.map((tag) => (
              <span key={tag} className="rounded bg-[var(--qq-bg-secondary)] px-1.5 py-0.5 text-xs text-[var(--qq-text-secondary)]">{tag}</span>
            ))}
          </div>
        ) : null}
      </div>
      <button
        onClick={onJoin}
        className="h-8 rounded-full border border-[var(--qq-border)] px-4 text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)]"
      >
        加入
      </button>
    </div>
  )
}

function GroupDetailPanel({ group, onClose, onJoin }: { group: Contact; onClose: () => void; onJoin: () => void }) {
  const memberText = `${group.memberCount ?? group.members?.length ?? 0}人`
  return (
    <div className="absolute inset-y-0 right-0 z-10 flex w-[310px] flex-col border-l border-[var(--qq-border)] bg-[var(--qq-bg)] shadow-[-8px_0_24px_rgba(0,0,0,0.08)]">
      <header className="flex h-9 items-center justify-end px-3">
        <button type="button" onClick={onClose} className="rounded p-1 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-secondary)]" aria-label="关闭群详情">
          <X size={15} />
        </button>
      </header>
      <div className="flex min-h-0 flex-1 flex-col px-4 pb-4">
        <div className="flex gap-3">
          <Avatar src={group.avatar} fallback={group.nickname} size={60} />
          <div className="min-w-0 flex-1 pt-1">
            <h3 className="truncate text-base font-semibold text-[var(--qq-text)]">{group.nickname}</h3>
            <p className="mt-1 text-xs text-[var(--qq-text-secondary)]">{group.id}（{memberText}）</p>
          </div>
        </div>
        {group.tags?.length ? (
          <div className="mt-4 flex flex-wrap gap-1.5">
            {group.tags.map((tag) => (
              <span key={tag} className="rounded bg-[var(--qq-bg-secondary)] px-2 py-1 text-xs text-[var(--qq-text)]">{tag}</span>
            ))}
          </div>
        ) : null}
        <div className="mt-6 space-y-5 text-sm">
          <DetailRow label="群名称" value={group.nickname} />
          <div className="flex items-center gap-4">
            <span className="w-16 shrink-0 text-[var(--qq-text-secondary)]">群介绍</span>
            <span className="min-w-0 flex-1 truncate text-[var(--qq-text)]">{group.signature || '在群里，发现更多～'}</span>
            <ChevronRight size={14} className="text-[var(--qq-text-tertiary)]" />
          </div>
          <DetailRow label="群分类" value={group.category || '未设置'} />
        </div>
        <div className="mt-auto grid grid-cols-2 gap-2 pt-6">
          <button type="button" className="rounded-lg border border-[var(--qq-border)] py-2 text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)]">分享</button>
          <button type="button" onClick={onJoin} className="rounded-lg bg-[var(--qq-primary)] py-2 text-sm font-medium text-white hover:bg-[var(--qq-primary-hover)]">申请加群</button>
        </div>
      </div>
    </div>
  )
}

function DetailRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex items-center gap-4">
      <span className="w-16 shrink-0 text-[var(--qq-text-secondary)]">{label}</span>
      <span className="min-w-0 flex-1 truncate text-[var(--qq-text)]">{value}</span>
    </div>
  )
}
