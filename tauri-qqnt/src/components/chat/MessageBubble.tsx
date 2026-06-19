import { Avatar } from '@/components/common/Avatar'
import { FileMessage } from './FileMessage'
import { cn } from '@/lib/utils'
import type { Message } from '@/types/qqnt'
import type { MentionCandidate } from './Composer'

function formatTime(ts?: number) {
  if (!ts) return ''
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

interface MessageBubbleProps {
  message: Message
  isSelf: boolean
  senderAvatar?: string
  onMentionSender?: (member: MentionCandidate) => void
  onRetry?: (id: string) => void
  onCancelFile?: (id: string) => void
  onDownloadFile?: (message: Message) => void
  onOpenFolder?: (message: Message) => void
}

export function MessageBubble({
  message,
  isSelf,
  senderAvatar,
  onMentionSender,
  onRetry,
  onCancelFile,
  onDownloadFile,
  onOpenFolder
}: MessageBubbleProps) {
  const isFile = message.type === 'file' || message.type === 'image'
  const statusText =
    message.status === 'sending'
      ? '发送中...'
      : message.status === 'failed'
        ? '发送失败（点击重试）'
        : formatTime(message.timestamp)

  function handleAvatarContextMenu(event: React.MouseEvent) {
    if (isSelf || !onMentionSender) return
    event.preventDefault()
    onMentionSender({ id: message.senderId, nickname: message.senderName, avatar: senderAvatar })
  }

  const avatar = (
    <div
      onContextMenu={handleAvatarContextMenu}
      title={isSelf ? message.senderName : `右键 @${message.senderName}`}
      className={cn('shrink-0', !isSelf && onMentionSender && 'cursor-context-menu')}
    >
      <Avatar src={senderAvatar} fallback={message.senderName} size={36} />
    </div>
  )

  return (
    <div className={cn('mb-4 flex items-start', isSelf ? 'justify-end' : 'justify-start')}>
      {!isSelf ? <div className="mr-3">{avatar}</div> : null}
      <div
        onClick={() => message.status === 'failed' && onRetry?.(message.id)}
        className={cn(
          'max-w-[60%] rounded-lg px-3 py-2 text-sm',
          isSelf ? 'bg-[var(--qq-primary)] text-white' : 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-text)]',
          message.status === 'failed' ? 'cursor-pointer hover:opacity-90' : 'cursor-default'
        )}
      >
        {isFile ? (
          <FileMessage
            message={message}
            isSelf={isSelf}
            onCancel={onCancelFile}
            onDownload={onDownloadFile}
            onOpenFolder={onOpenFolder}
          />
        ) : (
          <p className="whitespace-pre-wrap break-words">{message.content}</p>
        )}
        <span className="mt-1 block text-[10px] opacity-70">{statusText}</span>
      </div>
      {isSelf ? <div className="ml-3">{avatar}</div> : null}
    </div>
  )
}
