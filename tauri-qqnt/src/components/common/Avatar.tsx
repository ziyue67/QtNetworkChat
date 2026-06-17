export function Avatar({ src, alt, fallback, size = 40 }: { src?: string; alt?: string; fallback?: string; size?: number }) {
  return (
    <div
      className="flex shrink-0 items-center justify-center overflow-hidden rounded-full bg-[var(--qq-primary-soft)] text-[var(--qq-primary)] font-medium"
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