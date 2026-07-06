import { cn } from '@/lib/utils'

export function Badge({ children, className }: { children: React.ReactNode; className?: string }) {
  return (
    <span
      className={cn(
        'inline-flex h-5 min-w-5 items-center justify-center rounded-full bg-[var(--qq-danger)] px-1.5 text-[10px] font-medium text-white',
        className
      )}
    >
      {children}
    </span>
  )
}