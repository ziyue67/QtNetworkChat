import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import type { Message } from '@/types/qqnt'

function formatTime(ts?: number) {
  if (!ts) return ''
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

interface MessageBubbleProps {
  message: Message
  isSelf: boolean
}

export function MessageBubble({ message, isSelf }: MessageBubbleProps) {
  return (
    <div className={cn('mb-4 flex', isSelf ? 'justify-end' : 'justify-start')}>
      {!isSelf && <Avatar fallback={message.senderName} size={36} className="mr-3" />}
      <div
        className={cn(
          'max-w-[60%] rounded-lg px-3 py-2 text-sm',
          isSelf ? 'bg-[var(--qq-primary)] text-white' : 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-text)]'
        )}
      >
        <p>{message.content}</p>
        <span className="mt-1 block text-[10px] opacity-70">
          {message.status === 'sending' ? '发送中...' : message.status === 'failed' ? '发送失败' : formatTime(message.timestamp)}
        </span>
      </div>
    </div>
  )
}
