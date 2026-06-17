import { cn } from '@/lib/utils'

interface NavItemProps {
  icon: React.ReactNode
  label: string
  active?: boolean
  badge?: number
  mock?: boolean
  onClick?: () => void
}

export function NavItem({ icon, label, active, badge, mock, onClick }: NavItemProps) {
  return (
    <button
      onClick={onClick}
      className={cn(
        'group flex w-full flex-col items-center justify-center gap-1 rounded-lg px-2 py-2.5 text-[var(--qq-text-tertiary)] transition-colors',
        active
          ? 'bg-[var(--qq-primary-soft)] text-[var(--qq-primary)]'
          : 'hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]'
      )}
      title={label}
    >
      <div className="relative">
        {icon}
        {badge ? (
          <span className="absolute -right-2 -top-1 flex h-4 min-w-4 items-center justify-center rounded-full bg-[var(--qq-danger)] px-1 text-[10px] text-white">
            {badge > 99 ? '99+' : badge}
          </span>
        ) : null}
        {mock ? (
          <span className="absolute -bottom-1 -right-1 h-2 w-2 rounded-full bg-[var(--qq-warning)] ring-1 ring-[var(--qq-bg-secondary)]" />
        ) : null}
      </div>
      <span className="text-[10px]">{label}</span>
    </button>
  )
}
