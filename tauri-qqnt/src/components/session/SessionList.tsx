import type { Session } from '@/types/qqnt'
import { cn } from '@/lib/utils'
import { SessionItem } from './SessionItem'

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
      <div className={cn('flex-1 overflow-y-auto py-2', sessions.length === 0 && 'items-center justify-center')}>
        {sessions.length === 0 ? (
          <div className="flex h-full flex-col items-center justify-center px-6 text-center text-xs text-[var(--qq-text-tertiary)]">
            暂无会话
          </div>
        ) : (
          sessions.map((session) => (
            <SessionItem
              key={session.id}
              session={session}
              active={activeSessionId === session.id}
              onClick={() => onSelect(session.id)}
            />
          ))
        )}
      </div>
    </aside>
  )
}
