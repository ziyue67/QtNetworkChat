import { Paperclip, Image } from 'lucide-react'
import { useState } from 'react'
import { cn } from '@/lib/utils'

interface ComposerProps {
  onSend: (content: string) => void
  disabled?: boolean
  placeholder?: string
  canSendFiles?: boolean
  dragActive?: boolean
}

export function Composer({
  onSend,
  disabled,
  placeholder = '输入消息...',
  canSendFiles = false,
  dragActive = false
}: ComposerProps) {
  const [text, setText] = useState('')

  const handleSend = () => {
    const content = text.trim()
    if (!content || disabled) return
    onSend(content)
    setText('')
  }

  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSend()
    }
  }

  return (
    <div
      className={cn(
        'border-t border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] p-3 transition-colors',
        dragActive && 'bg-[var(--qq-primary-soft)]'
      )}
    >
      {canSendFiles ? (
        <div className="mb-2 flex items-center gap-2 text-xs text-[var(--qq-text-tertiary)]">
          <span className="inline-flex items-center gap-1 rounded-full bg-[var(--qq-bg)] px-2 py-1">
            <Paperclip size={12} /> 拖入文件发送
          </span>
          <span className="inline-flex items-center gap-1 rounded-full bg-[var(--qq-bg)] px-2 py-1">
            <Image size={12} /> 图片自动识别
          </span>
        </div>
      ) : null}
      <div className="flex items-end gap-2 rounded-lg bg-[var(--qq-bg)] p-2">
        <textarea
          value={text}
          onChange={(e) => setText(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={placeholder}
          rows={1}
          className="max-h-32 min-h-[40px] flex-1 resize-none bg-transparent px-2 py-2 text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
          disabled={disabled}
        />
        <button
          onClick={handleSend}
          disabled={!text.trim() || disabled}
          className="rounded-md bg-[var(--qq-primary)] px-4 py-2 text-sm font-medium text-white transition-opacity hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-40"
        >
          发送
        </button>
      </div>
    </div>
  )
}
