import type { Session } from '@/types/qqnt'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'

function formatTime(ts?: number) {
  if (!ts) return ''
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

interface SessionListProps {
  sessions: Session[]
  activeSessionId: string | null
  onSelect: (id: string) => void
}

export function SessionList({ sessions, activeSessionId, onSelect }: SessionListProps) {
  return (
    <aside className="flex w-[var(--qq-session-width)] flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
      <div className="flex h-[var(--qq-titlebar-height)] items-center border-b border-[var(--qq-border)] px-4">
        <h2 className="text-sm font-semibold text-[var(--qq-text)]">消息</h2>
      </div>
      <div className="flex-1 overflow-y-auto py-2">
        {sessions.map((session) => (
          <button
            key={session.id}
            onClick={() => onSelect(session.id)}
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
  )
}
