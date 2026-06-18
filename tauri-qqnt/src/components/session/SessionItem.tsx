import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import type { Session } from '@/types/qqnt'

function formatTime(ts?: number) {
  if (!ts) return ''
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

interface SessionItemProps {
  session: Session
  active: boolean
  onClick: () => void
}

export function SessionItem({ session, active, onClick }: SessionItemProps) {
  return (
    <button
      onClick={onClick}
      className={cn(
        'group flex w-full items-center gap-3 px-4 py-3 transition-colors',
        active ? 'bg-[var(--qq-bg-tertiary)]' : 'bg-transparent hover:bg-[var(--qq-bg-tertiary)]'
      )}
    >
      <div className="relative">
        <Avatar fallback={session.name} size={44} />
        {session.pinned ? (
          <span className="absolute -right-1 -top-1 flex h-3 w-3 items-center justify-center rounded-full bg-[var(--qq-warning)] text-[8px] text-white">
            ★
          </span>
        ) : null}
      </div>
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
              {session.unread > 99 ? '99+' : session.unread}
            </span>
          ) : null}
        </div>
      </div>
    </button>
  )
}
