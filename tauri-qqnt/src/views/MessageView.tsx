import { useSessionStore } from '@/stores/sessionStore'
import { useMessageStore } from '@/stores/messageStore'
import { useAuthStore } from '@/stores/authStore'
import { SessionList } from '@/components/session/SessionList'
import { ChatPanel } from '@/components/chat/ChatPanel'
import { sendPrivateMessage, sendGroupMessage, newReqId } from '@/api/qqnt'
import type { Message } from '@/types/qqnt'

export function MessageView() {
  const sessions = useSessionStore((state) => state.sessions)
  const activeSessionId = useSessionStore((state) => state.activeSessionId)
  const setActiveSession = useSessionStore((state) => state.setActiveSession)
  const markRead = useSessionStore((state) => state.markRead)
  const updateSession = useSessionStore((state) => state.updateSession)
  const messages = useMessageStore((state) => state.messages)
  const addMessage = useMessageStore((state) => state.addMessage)
  const updateMessageStatus = useMessageStore((state) => state.updateMessageStatus)
  const currentUser = useAuthStore((state) => state.currentUser)

  const activeSession = sessions.find((s) => s.id === activeSessionId)
  const activeMessages = activeSessionId ? messages[activeSessionId] || [] : []

  const handleSelect = (id: string) => {
    setActiveSession(id)
    markRead(id)
  }

  const handleSend = async (content: string) => {
    if (!activeSession || !currentUser) return

    const clientId = `client-${newReqId()}`
    const optimistic: Message = {
      id: clientId,
      sessionId: activeSession.id,
      senderId: currentUser.id,
      senderName: currentUser.nickname,
      type: 'text',
      content,
      timestamp: Date.now(),
      status: 'sending'
    }

    addMessage(activeSession.id, optimistic)
    updateSession(activeSession.id, { lastMessage: content, lastTime: optimistic.timestamp })

    try {
      const ack =
        activeSession.type === 'group'
          ? await sendGroupMessage(activeSession.id, content)
          : await sendPrivateMessage(activeSession.id, content)

      updateMessageStatus(activeSession.id, clientId, ack.status === 'ok' ? 'sent' : 'failed')
    } catch {
      updateMessageStatus(activeSession.id, clientId, 'failed')
    }
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <SessionList sessions={sessions} activeSessionId={activeSessionId} onSelect={handleSelect} />
      {activeSession ? (
        <ChatPanel
          session={activeSession}
          messages={activeMessages}
          currentUser={currentUser}
          onSend={handleSend}
        />
      ) : (
        <main className="flex min-w-0 flex-1 items-center justify-center text-sm text-[var(--qq-text-secondary)]">
          选择一个会话开始聊天
        </main>
      )}
    </div>
  )
}
