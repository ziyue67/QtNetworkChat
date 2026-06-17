import { useContactStore } from '@/stores/contactStore'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'

const STATUS_MAP: Record<string, string> = {
  online: '在线',
  offline: '离线',
  busy: '忙碌',
  away: '离开'
}

function ContactSection({ title, items }: { title: string; items: { id: string; nickname: string; status?: string; signature?: string }[] }) {
  return (
    <div className="mb-4">
      <h3 className="px-4 py-2 text-xs font-medium text-[var(--qq-text-tertiary)]">{title}</h3>
      {items.map((item) => (
        <div
          key={item.id}
          className="flex cursor-pointer items-center gap-3 px-4 py-3 transition-colors hover:bg-[var(--qq-bg-tertiary)]"
        >
          <Avatar fallback={item.nickname} size={42} />
          <div className="min-w-0 flex-1">
            <div className="flex items-center justify-between">
              <span className="truncate text-sm font-medium text-[var(--qq-text)]">{item.nickname}</span>
              <span
                className={cn(
                  'text-xs',
                  item.status === 'online' ? 'text-[var(--qq-success)]' : 'text-[var(--qq-text-tertiary)]'
                )}
              >
                {item.status ? STATUS_MAP[item.status] : '离线'}
              </span>
            </div>
            <p className="truncate text-xs text-[var(--qq-text-secondary)]">{item.signature || ' '}</p>
          </div>
        </div>
      ))}
    </div>
  )
}

export function ContactsView() {
  const contacts = useContactStore((state) => state.contacts)

  const friends = contacts.filter((c) => !c.id.startsWith('g-'))
  const groups = contacts.filter((c) => c.id.startsWith('g-'))

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <aside className="flex w-[var(--qq-session-width)] flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
        <div className="flex h-[var(--qq-titlebar-height)] items-center border-b border-[var(--qq-border)] px-4">
          <h2 className="text-sm font-semibold text-[var(--qq-text)]">联系人</h2>
        </div>
        <div className="flex-1 overflow-y-auto py-2">
          <ContactSection title={`好友 (${friends.length})`} items={friends} />
          <ContactSection title={`群聊 (${groups.length})`} items={groups} />
        </div>
      </aside>
      <main className="flex flex-1 items-center justify-center text-sm text-[var(--qq-text-secondary)]">
        选择一个联系人查看资料
      </main>
    </div>
  )
}