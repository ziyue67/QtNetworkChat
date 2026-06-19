import { useState } from 'react'
import type { Session, Message, User } from '@/types/qqnt'
import { MessageList } from './MessageList'
import { Composer, type MentionCandidate } from './Composer'

interface ChatPanelProps {
  session: Session
  messages: Message[]
  currentUser: User | null
  members?: MentionCandidate[]
  onSend: (content: string) => void
  onPickFiles?: () => void
  onPickImages?: () => void
  onRetry?: (id: string) => void
  onCancelFile?: (id: string) => void
  onDownloadFile?: (message: Message) => void
  onOpenFolder?: (message: Message) => void
  loading?: boolean
  dragActive?: boolean
  attachmentError?: string
  peerStatus?: User['status']
}

const STATUS_LABEL: Record<User['status'], string> = {
  online: '在线',
  offline: '离线',
  busy: '忙碌',
  away: '离开'
}

export function ChatPanel({
  session,
  messages,
  currentUser,
  members = [],
  onSend,
  onPickFiles,
  onPickImages,
  onRetry,
  onCancelFile,
  onDownloadFile,
  onOpenFolder,
  loading,
  dragActive,
  attachmentError,
  peerStatus
}: ChatPanelProps) {
  const [pendingMention, setPendingMention] = useState<MentionCandidate | null>(null)

  return (
    <main className="relative flex min-w-0 flex-1 flex-col">
      <div className="flex h-[var(--qq-titlebar-height)] items-center justify-between border-b border-[var(--qq-border)] px-4">
        <div>
          <span className="text-sm font-semibold text-[var(--qq-text)]">{session.name}</span>
          {peerStatus ? (
            <span className="ml-2 text-xs text-[var(--qq-text-tertiary)]">{STATUS_LABEL[peerStatus]}</span>
          ) : null}
        </div>
        {peerStatus === 'online' ? (
          <span className="rounded-full bg-[var(--qq-success)]/10 px-2 py-0.5 text-xs text-[var(--qq-success)]">在线</span>
        ) : null}
      </div>
      {dragActive ? (
        <div className="pointer-events-none absolute inset-x-4 bottom-24 top-14 z-10 flex items-center justify-center rounded-2xl border-2 border-dashed border-[var(--qq-primary)] bg-[var(--qq-primary-soft)] text-sm font-medium text-[var(--qq-primary)]">
          松开鼠标发送文件或图片
        </div>
      ) : null}
      <MessageList
        messages={messages}
        currentUser={currentUser}
        members={members}
        onMentionUser={setPendingMention}
        onRetry={onRetry}
        onCancelFile={onCancelFile}
        onDownloadFile={onDownloadFile}
        onOpenFolder={onOpenFolder}
      />
      <Composer
        onSend={onSend}
        onPickFiles={onPickFiles}
        onPickImages={onPickImages}
        disabled={loading || !currentUser}
        canSendFiles={Boolean(currentUser)}
        dragActive={dragActive}
        mentionCandidates={members.filter((member) => member.id !== currentUser?.id)}
        pendingMention={pendingMention}
        onMentionConsumed={() => setPendingMention(null)}
      />
      {attachmentError ? (
        <div className="border-t border-[var(--qq-border)] bg-[var(--qq-danger)]/10 px-4 py-2 text-xs text-[var(--qq-danger)]">
          {attachmentError}
        </div>
      ) : null}
    </main>
  )
}
