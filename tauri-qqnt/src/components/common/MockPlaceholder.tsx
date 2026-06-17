import { cn } from '@/lib/utils'

interface MockPlaceholderProps {
  title: string
  icon?: React.ReactNode
  className?: string
  children?: React.ReactNode
}

export function MockPlaceholder({ title, icon, className, children }: MockPlaceholderProps) {
  return (
    <div
      className={cn(
        'flex h-full flex-col items-center justify-center gap-4 bg-[var(--qq-bg)] p-8 text-center',
        className
      )}
    >
      {icon ? (
        <div className="flex h-16 w-16 items-center justify-center rounded-2xl bg-[var(--qq-bg-tertiary)] text-[var(--qq-primary)]">
          {icon}
        </div>
      ) : null}
      <div>
        <h2 className="text-lg font-semibold text-[var(--qq-text)]">{title}</h2>
        <p className="mt-1 text-sm text-[var(--qq-text-secondary)]">
          该页面为 Phase 2 占位实现，后续版本接入真实后端逻辑。
        </p>
      </div>
      {children}
    </div>
  )
}