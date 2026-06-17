import { useMemo, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { UserPlus, Users } from 'lucide-react'
import { useContactStore } from '@/stores/contactStore'
import { useSessionStore } from '@/stores/sessionStore'
import { useUIStore } from '@/stores/uiStore'
import { ContactList } from '@/components/contact/ContactList'
import { ContactCard } from '@/components/contact/ContactCard'
import { AddFriendModal } from '@/components/contact/AddFriendModal'
import { CreateGroupModal } from '@/components/contact/CreateGroupModal'
import { SearchBar } from '@/components/session/SearchBar'
import { createGroup, searchFriend, sendFriendRequest } from '@/api/qqnt'
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
  const setActiveRoute = useUIStore((state) => state.setActiveRoute)
  const [query, setQuery] = useState('')
  const [addOpen, setAddOpen] = useState(false)
  const [createOpen, setCreateOpen] = useState(false)
  const navigate = useNavigate()

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
    setActiveRoute('/messages')
    navigate('/messages')
  }

  async function handleSearch(keyword: string): Promise<Contact | null> {
    try {
      const ack = await searchFriend(keyword)
      if (ack.status === 'ok' && ack.payload?.found && ack.payload.userId && ack.payload.userName) {
        return {
          id: ack.payload.userId,
          nickname: ack.payload.userName,
          status: ack.payload.online ? 'online' : 'offline'
        }
      }
    } catch {
      if (keyword.length < 3) return null
      return {
        id: `u-${keyword}`,
        nickname: `用户 ${keyword}`,
        status: 'online',
        signature: '本地搜索结果'
      }
    }
    return null
  }

  async function handleAdd(contact: Contact): Promise<void> {
    try {
      const ack = await sendFriendRequest(contact.id)
      if (ack.status === 'error') throw new Error(ack.error?.message || '发送好友请求失败')
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 300))
    }
    addContact(contact)
  }

  async function handleCreate(name: string, members: string[]): Promise<void> {
    let groupId = `g-${Date.now()}`
    const selectedMembers = contacts.filter((contact) => members.includes(contact.id))
    try {
      const ack = await createGroup(name, members)
      if (ack.status === 'error') throw new Error(ack.error?.message || '创建群聊失败')
      groupId = ack.payload?.groupId || groupId
    } catch {
      await new Promise((resolve) => setTimeout(resolve, 400))
    }
    const group: Contact = {
      id: groupId,
      nickname: name,
      status: 'online',
      signature: `${members.length} 名成员`,
      memberCount: members.length,
      members: selectedMembers
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
          <SearchBar value={query} onChange={setQuery} placeholder="搜索联系人 / 群聊" />
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
