import { useEffect, useRef } from 'react'
import { useAuthStore } from '@/stores/authStore'
import { useSessionStore } from '@/stores/sessionStore'
import { useMessageStore } from '@/stores/messageStore'
import { useContactStore } from '@/stores/contactStore'
import type { Contact, Message, Session } from '@/types/qqnt'

let mockId = 0
function nextId(prefix: string) {
  mockId += 1
  return `${prefix}-${Date.now()}-${mockId}`
}

const MOCK_CONTACTS: Contact[] = [
  { id: 'u-1001', nickname: '阿强', status: 'online', signature: '在写代码' },
  { id: 'u-1002', nickname: '小美', status: 'busy', signature: '开会中' },
  { id: 'u-1003', nickname: '老王', status: 'offline', signature: '下班了' },
  { id: 'g-2001', nickname: '产品群', status: 'online', signature: '' }
]

const MOCK_SESSIONS: Session[] = [
  { id: 's-1001', type: 'private', name: '阿强', unread: 2, pinned: true },
  { id: 's-1002', type: 'private', name: '小美', unread: 0, pinned: false },
  { id: 'g-2001', type: 'group', name: '产品群', unread: 5, pinned: false, members: MOCK_CONTACTS.slice(0, 3) }
]

const MOCK_MESSAGES: Record<string, Message[]> = {
  's-1001': [
    {
      id: nextId('m'),
      sessionId: 's-1001',
      senderId: 'u-1001',
      senderName: '阿强',
      type: 'text',
      content: '晚上一起联调吗？',
      timestamp: Date.now() - 1000 * 60 * 5,
      status: 'sent'
    }
  ],
  's-1002': [
    {
      id: nextId('m'),
      sessionId: 's-1002',
      senderId: 'u-1002',
      senderName: '小美',
      type: 'text',
      content: '设计稿更新了，看一下。',
      timestamp: Date.now() - 1000 * 60 * 30,
      status: 'sent'
    }
  ],
  'g-2001': [
    {
      id: nextId('m'),
      sessionId: 'g-2001',
      senderId: 'u-1001',
      senderName: '阿强',
      type: 'text',
      content: '后端 ready 了吗？',
      timestamp: Date.now() - 1000 * 60 * 60,
      status: 'sent'
    }
  ]
}

export function useMockEngine() {
  const isAuthenticated = useAuthStore((state) => state.isAuthenticated)
  const isMock = useAuthStore((state) => state.mock)
  const { setContacts, updatePresence } = useContactStore()
  const { setSessions, updateSession } = useSessionStore()
  const { setMessages, addMessage } = useMessageStore()
  const initialized = useRef(false)

  useEffect(() => {
    if (!isAuthenticated || initialized.current || !isMock) return
    initialized.current = true

    // Seed initial data
    setContacts(MOCK_CONTACTS)
    setSessions(MOCK_SESSIONS)
    Object.entries(MOCK_MESSAGES).forEach(([sessionId, messages]) => {
      setMessages(sessionId, messages)
      const last = messages[messages.length - 1]
      if (last) {
        updateSession(sessionId, { lastMessage: last.content, lastTime: last.timestamp })
      }
    })

    // Simulate incoming events
    const timers: number[] = []
    const currentUser = useAuthStore.getState().currentUser

    timers.push(
      window.setInterval(() => {
        const sessionIds = Object.keys(MOCK_MESSAGES)
        const sessionId = sessionIds[Math.floor(Math.random() * sessionIds.length)]
        const session = useSessionStore.getState().sessions.find((s) => s.id === sessionId)
        const contact = MOCK_CONTACTS.find((c) => c.id !== currentUser?.id)
        if (!session || !contact) return

        const message: Message = {
          id: nextId('m'),
          sessionId,
          senderId: contact.id,
          senderName: contact.nickname,
          type: 'text',
          content: `mock event at ${new Date().toLocaleTimeString()}`,
          timestamp: Date.now(),
          status: 'sent'
        }
        addMessage(sessionId, message)
        updateSession(sessionId, {
          lastMessage: message.content,
          lastTime: message.timestamp,
          unread: session.unread + 1
        })
      }, 8000)
    )

    timers.push(
      window.setInterval(() => {
        const contact = MOCK_CONTACTS[Math.floor(Math.random() * MOCK_CONTACTS.length)]
        const statuses: Contact['status'][] = ['online', 'offline', 'busy', 'away']
        updatePresence(contact.id, statuses[Math.floor(Math.random() * statuses.length)])
      }, 12000)
    )

    return () => {
      timers.forEach((t) => clearInterval(t))
    }
  }, [isAuthenticated, isMock, setContacts, setSessions, setMessages, updateSession, updatePresence, addMessage])
}