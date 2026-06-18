import { Minus, Square, X, Maximize2 } from 'lucide-react'
import { useEffect, useState } from 'react'
import { getCurrentWindow } from '@tauri-apps/api/window'

interface WindowControlsProps {
  variant?: 'full' | 'close-only'
}

export function WindowControls({ variant = 'full' }: WindowControlsProps) {
  const [isMaximized, setIsMaximized] = useState(false)
  const showMinMax = variant === 'full'

  useEffect(() => {
    let unlisten: (() => void) | undefined
    let mounted = true

    async function bind() {
      try {
        const win = getCurrentWindow()
        const cleanup = await win.listen('tauri://resize', async () => {
          if (!mounted) return
          setIsMaximized(await win.isMaximized())
        })
        setIsMaximized(await win.isMaximized())
        unlisten = cleanup
      } catch {
        // Not running inside Tauri (e.g. browser preview).
      }
    }

    bind()
    return () => {
      mounted = false
      unlisten?.()
    }
  }, [])

  async function minimize() {
    try {
      await getCurrentWindow().minimize()
    } catch {
      // ignore
    }
  }

  async function toggleMaximize() {
    try {
      await getCurrentWindow().toggleMaximize()
    } catch {
      // ignore
    }
  }

  async function close() {
    try {
      await getCurrentWindow().close()
    } catch {
      // ignore
    }
  }

  return (
    <div className="flex h-full items-center" onMouseDown={(event) => event.stopPropagation()}>
      {showMinMax ? (
        <>
          <button
            onClick={minimize}
            className="flex h-10 w-12 items-center justify-center text-[var(--qq-text-secondary)] transition-colors hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
            aria-label="最小化"
          >
            <Minus size={14} strokeWidth={1.5} />
          </button>
          <button
            onClick={toggleMaximize}
            className="flex h-10 w-12 items-center justify-center text-[var(--qq-text-secondary)] transition-colors hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
            aria-label={isMaximized ? '还原' : '最大化'}
          >
            {isMaximized ? (
              <Square size={12} strokeWidth={1.5} />
            ) : (
              <Maximize2 size={12} strokeWidth={1.5} />
            )}
          </button>
        </>
      ) : null}
      <button
        onClick={close}
        className="flex h-10 w-12 items-center justify-center text-[var(--qq-text-secondary)] transition-colors hover:bg-[var(--qq-danger)] hover:text-white"
        aria-label="关闭"
      >
        <X size={14} strokeWidth={1.5} />
      </button>
    </div>
  )
}
