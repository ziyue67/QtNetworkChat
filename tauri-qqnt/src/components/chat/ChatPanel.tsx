import type { Session, Message, User } from '@/types/qqnt'
import { MessageList } from './MessageList'
import { Composer } from './Composer'

interface ChatPanelProps {
  session: Session
  messages: Message[]
  currentUser: User | null
  onSend: (content: string) => void
  loading?: boolean
}

export function ChatPanel({ session, messages, currentUser, onSend, loading }: ChatPanelProps) {
  return (
    <main className="flex min-w-0 flex-1 flex-col">
      <div className="flex h-[var(--qq-titlebar-height)] items-center border-b border-[var(--qq-border)] px-4">
        <span className="text-sm font-semibold text-[var(--qq-text)]">{session.name}</span>
      </div>
      <MessageList messages={messages} currentUser={currentUser} />
      <Composer onSend={onSend} disabled={loading || !currentUser} />
    </main>
  )
}
