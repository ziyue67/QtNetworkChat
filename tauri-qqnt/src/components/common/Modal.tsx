import { cn } from '@/lib/utils'
import { X } from 'lucide-react'
import { useEffect } from 'react'

interface ModalProps {
  open: boolean
  onClose: () => void
  title?: string
  children: React.ReactNode
  className?: string
  showClose?: boolean
}

export function Modal({ open, onClose, title, children, className, showClose = true }: ModalProps) {
  useEffect(() => {
    if (!open) return
    function onKeyDown(e: KeyboardEvent) {
      if (e.key === 'Escape') onClose()
    }
    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
  }, [open, onClose])

  if (!open) return null

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 p-4"
      onClick={(e) => {
        if (e.target === e.currentTarget) onClose()
      }}
    >
      <div
        className={cn(
          'max-h-[90vh] w-full max-w-md overflow-hidden rounded-xl bg-[var(--qq-bg)] shadow-[var(--qq-shadow)]',
          className
        )}
      >
        {title ? (
          <div className="flex items-center justify-between border-b border-[var(--qq-border)] px-4 py-3">
            <h3 className="text-base font-semibold text-[var(--qq-text)]">{title}</h3>
            {showClose ? (
              <button
                onClick={onClose}
                className="rounded p-1 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
              >
                <X size={16} />
              </button>
            ) : null}
          </div>
        ) : null}
        <div className="overflow-y-auto p-4">{children}</div>
      </div>
    </div>
  )
}
