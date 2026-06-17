import { useAuthStore } from '@/stores/authStore'
import { WindowControls } from './WindowControls'

interface TitleBarProps {
  variant?: 'full' | 'close-only'
}

export function TitleBar({ variant = 'full' }: TitleBarProps) {
  const currentUser = useAuthStore((state) => state.currentUser)
  const showUserInfo = variant === 'full'

  async function startDragging(event: React.MouseEvent<HTMLElement>) {
    if (event.button !== 0) return
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().startDragging()
    } catch {
      // Not running inside Tauri (e.g. browser preview).
    }
  }

  return (
    <header className="flex h-[var(--qq-titlebar-height)] shrink-0 items-center justify-between border-b border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
      <div
        className="flex flex-1 items-center gap-3 px-4 select-none"
        onMouseDown={startDragging}
        data-tauri-drag-region
      >
        <div className="flex h-6 w-6 items-center justify-center rounded bg-[var(--qq-primary)] text-white text-xs font-bold">
          Q
        </div>
        <span className="text-sm font-medium text-[var(--qq-text)]">QQ NT</span>
        {showUserInfo && currentUser ? (
          <span className="ml-2 text-xs text-[var(--qq-text-tertiary)]">
            {currentUser.nickname}
          </span>
        ) : null}
      </div>
      <WindowControls variant={variant} />
    </header>
  )
}
