import { useEffect, useMemo, useRef, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { Bell, ChevronRight, FileUp, Plus, Search, UserCog, UserPlus, Users } from 'lucide-react'
import { useContactStore } from '@/stores/contactStore'
import { useSessionStore } from '@/stores/sessionStore'
import { useUIStore } from '@/stores/uiStore'
import { ContactList } from '@/components/contact/ContactList'
import { ContactCard } from '@/components/contact/ContactCard'
import { CreateGroupModal } from '@/components/contact/CreateGroupModal'
import { FriendManagerModal } from '@/components/contact/FriendManagerModal'
import { GlobalSearchModal } from '@/components/contact/GlobalSearchModal'
import { ContactNoticePanel } from '@/components/contact/ContactNoticePanel'
import { cn } from '@/lib/utils'
import type { Contact } from '@/types/qqnt'

type ContactTab = 'friends' | 'groups'

export function ContactsView() {
  const contacts = useContactStore((state) => state.contacts)
  const groups = useContactStore((state) => state.groups)
  const selectedId = useContactStore((state) => state.selectedContactId)
  const setSelectedContactId = useContactStore((state) => state.setSelectedContactId)
  const upsertSession = useSessionStore((state) => state.upsertSession)
  const setActiveSession = useSessionStore((state) => state.setActiveSession)
  const friendNoticeUnread = useSessionStore((state) =>
    state.sessions.some((session) => session.type === 'private' && session.unread > 0)
  )
  const groupNoticeUnread = useSessionStore((state) =>
    state.sessions.some((session) => session.type === 'group' && session.unread > 0)
  )
  const setActiveRoute = useUIStore((state) => state.setActiveRoute)
  const navigate = useNavigate()

  const [activeTab, setActiveTab] = useState<ContactTab>('friends')
  const [plusOpen, setPlusOpen] = useState(false)
  const [createOpen, setCreateOpen] = useState(false)
  const [managerOpen, setManagerOpen] = useState(false)
  const [searchOpen, setSearchOpen] = useState(false)
  const [noticeOpen, setNoticeOpen] = useState<'friend' | 'group' | null>(null)
  const plusRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!plusOpen) return
    function onKeyDown(e: KeyboardEvent) {
      if (e.key === 'Escape') setPlusOpen(false)
    }
    function onClick(e: MouseEvent) {
      if (plusRef.current && !plusRef.current.contains(e.target as Node)) setPlusOpen(false)
    }
    document.addEventListener('keydown', onKeyDown)
    document.addEventListener('mousedown', onClick)
    return () => {
      document.removeEventListener('keydown', onKeyDown)
      document.removeEventListener('mousedown', onClick)
    }
  }, [plusOpen])

  const selectedContact = useMemo(() => {
    return [...contacts, ...groups].find((contact) => contact.id === selectedId) || null
  }, [contacts, groups, selectedId])

  function handleSendMessage(contact: Contact) {
    const isGroup = groups.some((group) => group.id === contact.id) || contact.id.startsWith('g-')
    upsertSession({
      id: contact.id,
      type: isGroup ? 'group' : 'private',
      name: contact.nickname,
      avatar: contact.avatar,
      unread: 0,
      pinned: false,
      members: contact.members
    })
    setActiveSession(contact.id)
    setActiveRoute('/messages')
    navigate('/messages')
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <aside className="flex w-[var(--qq-session-width)] flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
        <div className="border-b border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] p-2">
          <div className="flex items-center gap-2">
            <div className="flex h-7 min-w-0 flex-1 items-center gap-2 rounded-md bg-[var(--qq-bg)] px-2 text-xs text-[var(--qq-text-tertiary)] transition-colors hover:bg-[var(--qq-bg-tertiary)]">
              <Search size={14} />
              <input
                readOnly
                value=""
                onFocus={() => setSearchOpen(true)}
                onClick={() => setSearchOpen(true)}
                placeholder="搜索"
                className="min-w-0 flex-1 cursor-pointer bg-transparent text-sm outline-none placeholder:text-[var(--qq-text-tertiary)]"
              />
            </div>
            <div ref={plusRef} className="relative">
              <button
                onClick={() => setPlusOpen((open) => !open)}
                className={cn(
                  'flex h-7 w-7 items-center justify-center rounded-md text-[var(--qq-text-secondary)] transition-colors',
                  plusOpen ? 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-primary)]' : 'bg-[var(--qq-bg)] hover:bg-[var(--qq-bg-tertiary)]'
                )}
                aria-label="更多"
                aria-expanded={plusOpen}
              >
                <Plus size={17} />
              </button>
              {plusOpen ? (
                <div className="absolute right-0 top-full z-40 mt-1 w-32 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] py-1 shadow-[var(--qq-shadow)]">
                  <button
                    onClick={() => {
                      setPlusOpen(false)
                      setCreateOpen(true)
                    }}
                    className="flex w-full items-center gap-2 px-3 py-2 text-left text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)]"
                  >
                    <Users size={14} />
                    创建群聊
                  </button>
                  <button
                    onClick={() => {
                      setPlusOpen(false)
                      setSearchOpen(true)
                    }}
                    className="flex w-full items-center gap-2 px-3 py-2 text-left text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)]"
                  >
                    <UserPlus size={14} />
                    加好友/群
                  </button>
                  <button
                    disabled
                    title="即将上线"
                    className="flex w-full cursor-not-allowed items-center gap-2 px-3 py-2 text-left text-sm text-[var(--qq-text-tertiary)]"
                  >
                    <FileUp size={14} />
                    闪传文件
                  </button>
                </div>
              ) : null}
            </div>
          </div>

          <button
            onClick={() => setManagerOpen(true)}
            className="mt-3 flex h-8 w-full items-center justify-center gap-2 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] text-sm font-medium text-[var(--qq-text)] transition-colors hover:bg-[var(--qq-bg-tertiary)]"
          >
            <UserCog size={15} />
            好友管理器
          </button>

          <button
            onClick={() => {
              setSelectedContactId(null)
              setNoticeOpen('friend')
            }}
            className="mt-3 flex h-9 w-full items-center justify-between px-1 text-sm text-[var(--qq-text)] hover:text-[var(--qq-primary)]"
          >
            <span className="flex items-center gap-2">
              <Bell size={14} className="text-[var(--qq-text-secondary)]" />
              好友通知
              <span
                data-testid="friend-notice-dot"
                className={cn('h-1.5 w-1.5 rounded-full bg-[var(--qq-danger)]', !friendNoticeUnread && 'hidden')}
              />
            </span>
            <ChevronRight size={15} className="text-[var(--qq-text-tertiary)]" />
          </button>
          <button
            onClick={() => {
              setSelectedContactId(null)
              setNoticeOpen('group')
            }}
            className="flex h-9 w-full items-center justify-between px-1 text-sm text-[var(--qq-text)] hover:text-[var(--qq-primary)]"
          >
            <span className="flex items-center gap-2">
              <Users size={14} className="text-[var(--qq-text-secondary)]" />
              群通知
              <span
                data-testid="group-notice-dot"
                className={cn('h-1.5 w-1.5 rounded-full bg-[var(--qq-danger)]', !groupNoticeUnread && 'hidden')}
              />
            </span>
            <ChevronRight size={15} className="text-[var(--qq-text-tertiary)]" />
          </button>

          <div className="mt-3 grid grid-cols-2 rounded-md bg-[var(--qq-bg)] p-1">
            <button
              onClick={() => setActiveTab('friends')}
              className={cn(
                'rounded py-1.5 text-sm transition-colors',
                activeTab === 'friends'
                  ? 'bg-[var(--qq-bg-secondary)] font-medium text-[var(--qq-primary)] shadow-sm'
                  : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
              )}
            >
              好友
            </button>
            <button
              onClick={() => setActiveTab('groups')}
              className={cn(
                'rounded py-1.5 text-sm transition-colors',
                activeTab === 'groups'
                  ? 'bg-[var(--qq-bg-secondary)] font-medium text-[var(--qq-primary)] shadow-sm'
                  : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
              )}
            >
              群聊
            </button>
          </div>
        </div>

        <div className="flex-1 overflow-y-auto py-2">
          {activeTab === 'friends' ? (
            <ContactList
              title={`好友 (${contacts.length})`}
              items={contacts}
              selectedId={selectedId}
              onSelect={(contact) => {
                setNoticeOpen(null)
                setSelectedContactId(contact.id)
              }}
            />
          ) : (
            <ContactList
              title={`群聊 (${groups.length})`}
              items={groups}
              selectedId={selectedId}
              onSelect={(contact) => {
                setNoticeOpen(null)
                setSelectedContactId(contact.id)
              }}
              isGroup
            />
          )}
        </div>
      </aside>
      <main className="min-h-0 flex-1">
        {noticeOpen ? (
          <ContactNoticePanel type={noticeOpen} />
        ) : selectedContact ? (
          <ContactCard
            contact={selectedContact}
            isGroup={groups.some((group) => group.id === selectedContact.id) || selectedContact.id.startsWith('g-')}
            onSendMessage={handleSendMessage}
          />
        ) : (
          <div className="flex h-full items-center justify-center text-sm text-[var(--qq-text-secondary)]">
            选择一个联系人查看资料
          </div>
        )}
      </main>
      <GlobalSearchModal open={searchOpen} onClose={() => setSearchOpen(false)} />
      <FriendManagerModal open={managerOpen} onClose={() => setManagerOpen(false)} />
      <CreateGroupModal open={createOpen} onClose={() => setCreateOpen(false)} />
    </div>
  )
}




