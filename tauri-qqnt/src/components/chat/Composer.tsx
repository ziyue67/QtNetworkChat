import { Paperclip, Image } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { Avatar } from '@/components/common/Avatar'
import { cn } from '@/lib/utils'

export interface MentionCandidate {
  id: string
  nickname: string
  avatar?: string
}

interface ComposerProps {
  onSend: (content: string) => void
  onPickFiles?: () => void
  onPickImages?: () => void
  disabled?: boolean
  placeholder?: string
  canSendFiles?: boolean
  dragActive?: boolean
  mentionCandidates?: MentionCandidate[]
  pendingMention?: MentionCandidate | null
  onMentionConsumed?: () => void
}

function uniqueCandidates(candidates: MentionCandidate[]) {
  const seen = new Set<string>()
  return candidates.filter((candidate) => {
    if (seen.has(candidate.id)) return false
    seen.add(candidate.id)
    return true
  })
}

function mentionToken(text: string, cursor: number) {
  const beforeCursor = text.slice(0, cursor)
  const match = beforeCursor.match(/(^|\s)@([^@\s]*)$/)
  if (!match) return null
  return {
    start: beforeCursor.length - match[2].length - 1,
    query: match[2]
  }
}

export function Composer({
  onSend,
  onPickFiles,
  onPickImages,
  disabled,
  placeholder = '输入消息...',
  canSendFiles = false,
  dragActive = false,
  mentionCandidates = [],
  pendingMention,
  onMentionConsumed
}: ComposerProps) {
  const [text, setText] = useState('')
  const [cursor, setCursor] = useState(0)
  const textareaRef = useRef<HTMLTextAreaElement>(null)
  const candidates = uniqueCandidates(mentionCandidates)
  const activeToken = mentionToken(text, cursor)
  const filteredCandidates = activeToken
    ? candidates.filter((candidate) => candidate.nickname.toLowerCase().includes(activeToken.query.toLowerCase()))
    : []
  const showMentionPanel = Boolean(activeToken && filteredCandidates.length > 0 && !disabled)

  function insertMention(member: MentionCandidate) {
    const mention = `@${member.nickname} `
    const token = mentionToken(text, textareaRef.current?.selectionStart ?? cursor)
    const start = token?.start ?? text.length
    const end = textareaRef.current?.selectionStart ?? cursor
    const nextText = `${text.slice(0, start)}${mention}${text.slice(end)}`
    const nextCursor = start + mention.length
    setText(nextText)
    setCursor(nextCursor)
    requestAnimationFrame(() => {
      textareaRef.current?.focus()
      textareaRef.current?.setSelectionRange(nextCursor, nextCursor)
    })
  }

  useEffect(() => {
    if (!pendingMention) return
    insertMention(pendingMention)
    onMentionConsumed?.()
  }, [pendingMention])

  const handleSend = () => {
    const content = text.trim()
    if (!content || disabled) return
    onSend(content)
    setText('')
    setCursor(0)
  }

  const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      handleSend()
    }
  }

  const updateCursor = (target: HTMLTextAreaElement) => {
    setCursor(target.selectionStart)
  }

  return (
    <div
      className={cn(
        'border-t border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] p-3 transition-colors',
        dragActive && 'bg-[var(--qq-primary-soft)]'
      )}
    >
      {canSendFiles ? (
        <div className="mb-2 flex flex-wrap items-center gap-2 text-xs text-[var(--qq-text-tertiary)]">
          <button
            type="button"
            onClick={onPickFiles}
            disabled={disabled}
            className="inline-flex items-center gap-1 rounded-full bg-[var(--qq-bg)] px-2 py-1 hover:text-[var(--qq-primary)] disabled:opacity-50"
          >
            <Paperclip size={12} /> 发送文件
          </button>
          <button
            type="button"
            onClick={onPickImages}
            disabled={disabled}
            className="inline-flex items-center gap-1 rounded-full bg-[var(--qq-bg)] px-2 py-1 hover:text-[var(--qq-primary)] disabled:opacity-50"
          >
            <Image size={12} /> 发送图片
          </button>
          <span className="inline-flex items-center gap-1 rounded-full bg-[var(--qq-bg)] px-2 py-1">
            <Paperclip size={12} /> 拖入文件发送
          </span>
        </div>
      ) : null}
      <div className="relative">
        {showMentionPanel ? (
          <div className="absolute bottom-full left-0 z-20 mb-2 max-h-52 w-60 overflow-auto rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-2 shadow-[var(--qq-shadow)]">
            <div className="mb-1 px-2 text-[10px] text-[var(--qq-text-tertiary)]">选择要 @ 的成员</div>
            {filteredCandidates.map((candidate) => (
              <button
                key={candidate.id}
                type="button"
                onMouseDown={(event) => event.preventDefault()}
                onClick={() => insertMention(candidate)}
                className="flex w-full items-center gap-2 rounded-lg px-2 py-1.5 text-left text-xs text-[var(--qq-text)] hover:bg-[var(--qq-primary-soft)]"
              >
                <Avatar src={candidate.avatar} fallback={candidate.nickname} size={24} />
                <span className="min-w-0 flex-1 truncate">{candidate.nickname}</span>
              </button>
            ))}
          </div>
        ) : null}
        <div className="flex items-end gap-2 rounded-lg bg-[var(--qq-bg)] p-2">
          <textarea
            ref={textareaRef}
            value={text}
            onChange={(e) => {
              setText(e.target.value)
              updateCursor(e.target)
            }}
            onClick={(e) => updateCursor(e.currentTarget)}
            onKeyUp={(e) => updateCursor(e.currentTarget)}
            onSelect={(e) => updateCursor(e.currentTarget)}
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
    </div>
  )
}
