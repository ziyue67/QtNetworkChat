import { useState, useEffect, useRef } from 'react'
import { useSessionStore } from '@/stores/sessionStore'
import { useMessageStore } from '@/stores/messageStore'
import { useAuthStore } from '@/stores/authStore'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import { sendPrivateMessage, sendGroupMessage, newReqId } from '@/api/qqnt'
import type { Message } from '@/types/qqnt'

function formatTime(ts?: number) {
  if (!ts) return ''
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

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

  const [text, setText] = useState('')
  const messagesEndRef = useRef<HTMLDivElement>(null)

  const activeSession = sessions.find((s) => s.id === activeSessionId)
  const activeMessages = activeSessionId ? messages[activeSessionId] || [] : []

  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [activeMessages.length, activeSessionId])

  const handleSend = async () => {
    const content = text.trim()
    if (!activeSession || !content || !currentUser) return

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
    setText('')

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

  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSend()
    }
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <aside className="flex w-[var(--qq-session-width)] flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
        <div className="flex h-[var(--qq-titlebar-height)] items-center border-b border-[var(--qq-border)] px-4">
          <h2 className="text-sm font-semibold text-[var(--qq-text)]">消息</h2>
        </div>
        <div className="flex-1 overflow-y-auto py-2">
          {sessions.map((session) => (
            <button
              key={session.id}
              onClick={() => {
                setActiveSession(session.id)
                markRead(session.id)
              }}
              className={cn(
                'flex w-full items-center gap-3 px-4 py-3 transition-colors hover:bg-[var(--qq-bg-tertiary)]',
                activeSessionId === session.id ? 'bg-[var(--qq-bg-tertiary)]' : 'bg-transparent'
              )}
            >
              <Avatar fallback={session.name} size={44} />
              <div className="min-w-0 flex-1 text-left">
                <div className="flex items-center justify-between">
                  <span className="truncate text-sm font-medium text-[var(--qq-text)]">{session.name}</span>
                  {session.lastTime ? (
                    <span className="text-xs text-[var(--qq-text-tertiary)]">{formatTime(session.lastTime)}</span>
                  ) : null}
                </div>
                <div className="flex items-center justify-between">
                  <span className="truncate text-xs text-[var(--qq-text-secondary)]">
                    {session.lastMessage || '暂无消息'}
                  </span>
                  {session.unread > 0 ? (
                    <span className="ml-2 flex h-5 min-w-5 items-center justify-center rounded-full bg-[var(--qq-danger)] px-1 text-xs text-white">
                      {session.unread}
                    </span>
                  ) : null}
                </div>
              </div>
            </button>
          ))}
        </div>
      </aside>

      <main className="flex min-w-0 flex-1 flex-col">
        {activeSession ? (
          <>
            <div className="flex h-[var(--qq-titlebar-height)] items-center border-b border-[var(--qq-border)] px-4">
              <span className="text-sm font-semibold text-[var(--qq-text)]">{activeSession.name}</span>
            </div>
            <div className="flex-1 overflow-y-auto p-4">
              {activeMessages.map((msg) => {
                const isSelf = msg.senderId === currentUser?.id
                return (
                  <div key={msg.id} className={cn('mb-4 flex', isSelf ? 'justify-end' : 'justify-start')}>
                    {!isSelf && <Avatar fallback={msg.senderName} size={36} className="mr-3" />}
                    <div
                      className={cn(
                        'max-w-[60%] rounded-lg px-3 py-2 text-sm',
                        isSelf ? 'bg-[var(--qq-primary)] text-white' : 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-text)]'
                      )}
                    >
                      <p>{msg.content}</p>
                      <span className="mt-1 block text-[10px] opacity-70">
                        {msg.status === 'sending' ? '发送中...' : msg.status === 'failed' ? '发送失败' : formatTime(msg.timestamp)}
                      </span>
                    </div>
                  </div>
                )
              })}
              <div ref={messagesEndRef} />
            </div>
            <div className="border-t border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] p-3">
              <div className="flex items-end gap-2 rounded-lg bg-[var(--qq-bg)] p-2">
                <textarea
                  value={text}
                  onChange={(e) => setText(e.target.value)}
                  onKeyDown={handleKeyDown}
                  placeholder="输入消息..."
                  rows={1}
                  className="max-h-32 min-h-[40px] flex-1 resize-none bg-transparent px-2 py-2 text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
                />
                <button
                  onClick={handleSend}
                  disabled={!text.trim() || !currentUser}
                  className="rounded-md bg-[var(--qq-primary)] px-4 py-2 text-sm font-medium text-white transition-opacity hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-40"
                >
                  发送
                </button>
              </div>
            </div>
          </>
        ) : (
          <div className="flex h-full items-center justify-center text-sm text-[var(--qq-text-secondary)]">
            选择一个会话开始聊天
          </div>
        )}
      </main>
    </div>
  )
}
