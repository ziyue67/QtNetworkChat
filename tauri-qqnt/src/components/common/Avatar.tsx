import { cn } from '@/lib/utils'

export function Avatar({
  src,
  alt,
  fallback,
  size = 40,
  className
}: {
  src?: string
  alt?: string
  fallback?: string
  size?: number
  className?: string
}) {
  return (
    <div
      className={cn(
        'flex shrink-0 items-center justify-center overflow-hidden rounded-full bg-[var(--qq-primary-soft)] font-medium text-[var(--qq-primary)]',
        className
      )}
      style={{ width: size, height: size, fontSize: size * 0.4 }}
    >
      {src ? (
        <img src={src} alt={alt || 'avatar'} className="h-full w-full object-cover" />
      ) : (
        fallback?.[0]?.toUpperCase() || '?'
      )}
    </div>
  )
}