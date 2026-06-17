import type { Session, Message, User } from '@/types/qqnt'
import { MessageList } from './MessageList'
import { Composer } from './Composer'

interface ChatPanelProps {
  session: Session
  messages: Message[]
  currentUser: User | null
  onSend: (content: string) => void
  onRetry?: (id: string) => void
  onCancelFile?: (id: string) => void
  onDownloadFile?: (message: Message) => void
  onOpenFolder?: (message: Message) => void
  loading?: boolean
}

export function ChatPanel({
  session,
  messages,
  currentUser,
  onSend,
  onRetry,
  onCancelFile,
  onDownloadFile,
  onOpenFolder,
  loading
}: ChatPanelProps) {
  return (
    <main className="flex min-w-0 flex-1 flex-col">
      <div className="flex h-[var(--qq-titlebar-height)] items-center border-b border-[var(--qq-border)] px-4">
        <span className="text-sm font-semibold text-[var(--qq-text)]">{session.name}</span>
      </div>
      <MessageList
        messages={messages}
        currentUser={currentUser}
        onRetry={onRetry}
        onCancelFile={onCancelFile}
        onDownloadFile={onDownloadFile}
        onOpenFolder={onOpenFolder}
      />
      <Composer onSend={onSend} disabled={loading || !currentUser} />
    </main>
  )
}
