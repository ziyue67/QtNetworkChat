import { useMemo, useState } from 'react'
import { Search, UserPlus, Users } from 'lucide-react'
import { useContactStore } from '@/stores/contactStore'
import { useSessionStore } from '@/stores/sessionStore'
import { ContactList } from '@/components/contact/ContactList'
import { ContactCard } from '@/components/contact/ContactCard'
import { AddFriendModal } from '@/components/contact/AddFriendModal'
import { CreateGroupModal } from '@/components/contact/CreateGroupModal'
import type { Contact } from '@/types/qqnt'

export function ContactsView() {
  const contacts = useContactStore((state) => state.contacts)
  const groups = useContactStore((state) => state.groups)
  const selectedId = useContactStore((state) => state.selectedContactId)
  const setSelectedContactId = useContactStore((state) => state.setSelectedContactId)
  const addContact = useContactStore((state) => state.addContact)
  const addGroup = useContactStore((state) => state.addGroup)
  const setSessions = useSessionStore((state) => state.setSessions)
  const sessions = useSessionStore((state) => state.sessions)
  const [query, setQuery] = useState('')
  const [addOpen, setAddOpen] = useState(false)
  const [createOpen, setCreateOpen] = useState(false)

  const selectedContact = useMemo(() => {
    return [...contacts, ...groups].find((c) => c.id === selectedId) || null
  }, [contacts, groups, selectedId])

  const filteredFriends = useMemo(() => {
    const list = contacts.filter((c) => !c.id.startsWith('g-'))
    if (!query.trim()) return list
    const q = query.trim().toLowerCase()
    return list.filter((c) => c.nickname.toLowerCase().includes(q) || c.id.toLowerCase().includes(q))
  }, [contacts, query])

  const filteredGroups = useMemo(() => {
    if (!query.trim()) return groups
    const q = query.trim().toLowerCase()
    return groups.filter((g) => g.nickname.toLowerCase().includes(q) || g.id.toLowerCase().includes(q))
  }, [groups, query])

  function handleSendMessage(contact: Contact) {
    const sessionId = contact.id
    if (!sessions.find((s) => s.id === sessionId)) {
      setSessions([
        ...sessions,
        {
          id: sessionId,
          type: contact.id.startsWith('g-') ? 'group' : 'private',
          name: contact.nickname,
          avatar: contact.avatar,
          unread: 0,
          pinned: false
        }
      ])
    }
    // TODO: navigate to messages and activate session
  }

  async function handleSearch(keyword: string): Promise<Contact | null> {
    // 真实后端接入后换成 api/qqnt.searchFriend
    await new Promise((resolve) => setTimeout(resolve, 400))
    if (keyword.length < 3) return null
    return {
      id: `u-${keyword}`,
      nickname: `用户 ${keyword}`,
      status: 'online',
      signature: '这是 mock 搜索结果'
    }
  }

  async function handleAdd(contact: Contact): Promise<void> {
    await new Promise((resolve) => setTimeout(resolve, 300))
    addContact(contact)
  }

  async function handleCreate(name: string, members: string[]): Promise<void> {
    await new Promise((resolve) => setTimeout(resolve, 400))
    const group: Contact = {
      id: `g-${Date.now()}`,
      nickname: name,
      status: 'online',
      signature: `${members.length} 名成员`
    }
    addGroup(group)
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <aside className="flex w-[var(--qq-session-width)] flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
        <div className="flex h-[var(--qq-titlebar-height)] items-center justify-between border-b border-[var(--qq-border)] px-4">
          <h2 className="text-sm font-semibold text-[var(--qq-text)]">联系人</h2>
          <div className="flex gap-1">
            <button
              onClick={() => setAddOpen(true)}
              className="rounded p-1.5 text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-primary)]"
              title="添加好友"
            >
              <UserPlus size={16} />
            </button>
            <button
              onClick={() => setCreateOpen(true)}
              className="rounded p-1.5 text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-primary)]"
              title="创建群聊"
            >
              <Users size={16} />
            </button>
          </div>
        </div>
        <div className="border-b border-[var(--qq-border)] p-3">
          <div className="flex items-center gap-2 rounded-lg bg-[var(--qq-bg)] px-3 py-2">
            <Search size={14} className="text-[var(--qq-text-tertiary)]" />
            <input
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder="搜索联系人 / 群聊"
              className="flex-1 bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
            />
          </div>
        </div>
        <div className="flex-1 overflow-y-auto py-2">
          <ContactList
            title={`好友 (${filteredFriends.length})`}
            items={filteredFriends}
            selectedId={selectedId}
            onSelect={(c) => setSelectedContactId(c.id)}
          />
          <ContactList
            title={`群聊 (${filteredGroups.length})`}
            items={filteredGroups}
            selectedId={selectedId}
            onSelect={(c) => setSelectedContactId(c.id)}
            isGroup
          />
        </div>
      </aside>
      <main className="min-h-0 flex-1">
        {selectedContact ? (
          <ContactCard
            contact={selectedContact}
            isGroup={selectedContact.id.startsWith('g-')}
            onSendMessage={handleSendMessage}
          />
        ) : (
          <div className="flex h-full items-center justify-center text-sm text-[var(--qq-text-secondary)]">
            选择一个联系人查看资料
          </div>
        )}
      </main>
      <AddFriendModal
        open={addOpen}
        onClose={() => setAddOpen(false)}
        onSearch={handleSearch}
        onAdd={handleAdd}
      />
      <CreateGroupModal open={createOpen} onClose={() => setCreateOpen(false)} onCreate={handleCreate} />
    </div>
  )
}
