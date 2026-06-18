import { useAuthStore } from '@/stores/authStore'
import { startWindowDrag } from '@/lib/window'
import { WindowControls } from './WindowControls'

interface TitleBarProps {
  variant?: 'full' | 'close-only'
}

export function TitleBar({ variant = 'full' }: TitleBarProps) {
  const currentUser = useAuthStore((state) => state.currentUser)
  const showUserInfo = variant === 'full'

  return (
    <header
      className="flex h-[var(--qq-titlebar-height)] shrink-0 items-center justify-between border-b border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] select-none"
      onMouseDown={startWindowDrag}
      data-tauri-drag-region
    >
      <div className="flex flex-1 items-center gap-3 px-4" data-tauri-drag-region>
        <div className="flex h-6 w-6 items-center justify-center rounded bg-[var(--qq-primary)] text-xs font-bold text-white">
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
