import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'
import type { Contact } from '@/types/qqnt'

const STATUS_MAP: Record<string, string> = {
  online: '在线',
  offline: '离线',
  busy: '忙碌',
  away: '离开'
}

interface ContactListProps {
  title: string
  items: Contact[]
  selectedId?: string | null
  onSelect?: (contact: Contact) => void
  isGroup?: boolean
}

export function ContactList({ title, items, selectedId, onSelect, isGroup = false }: ContactListProps) {
  return (
    <div className="mb-2">
      <h3 className="px-4 py-2 text-xs font-medium text-[var(--qq-text-tertiary)]">{title}</h3>
      {items.map((item) => (
        <div
          key={item.id}
          onClick={() => onSelect?.(item)}
          className={cn(
            'flex cursor-pointer items-center gap-3 px-4 py-2.5 transition-colors',
            selectedId === item.id ? 'bg-[var(--qq-primary-soft)]' : 'hover:bg-[var(--qq-bg-tertiary)]'
          )}
        >
          <Avatar src={item.avatar} fallback={item.nickname} size={40} />
          <div className="min-w-0 flex-1">
            <div className="flex items-center justify-between">
              <span className="truncate text-sm font-medium text-[var(--qq-text)]">{item.nickname}</span>
              {!isGroup ? (
                <span
                  className={cn(
                    'text-xs',
                    item.status === 'online' ? 'text-[var(--qq-success)]' : 'text-[var(--qq-text-tertiary)]'
                  )}
                >
                  {STATUS_MAP[item.status] || '离线'}
                </span>
              ) : null}
            </div>
            <p className="truncate text-xs text-[var(--qq-text-secondary)]">{item.signature || ' '}</p>
          </div>
        </div>
      ))}
    </div>
  )
}
