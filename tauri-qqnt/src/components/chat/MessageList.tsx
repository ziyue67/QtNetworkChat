import { useEffect, useRef } from 'react'
import type { Message, User } from '@/types/qqnt'
import { MessageBubble } from './MessageBubble'

interface MessageListProps {
  messages: Message[]
  currentUser: User | null
}

export function MessageList({ messages, currentUser }: MessageListProps) {
  const endRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages.length])

  return (
    <div className="flex-1 overflow-y-auto p-4">
      {messages.map((msg) => (
        <MessageBubble key={msg.id} message={msg} isSelf={msg.senderId === currentUser?.id} />
      ))}
      <div ref={endRef} />
    </div>
  )
}
