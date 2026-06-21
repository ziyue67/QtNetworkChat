import {
  ChevronDown,
  Clock3,
  Folder,
  Image,
  Scissors,
  Send,
  Smile,
} from 'lucide-react'
import { useEffect, useMemo, useRef, useState } from 'react'
import type { Message } from '@/types/qqnt'
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
  onScreenshot?: () => void
  screenshotBusy?: boolean
  screenshotShortcutLabel?: string
  hideWindowBeforeScreenshot?: boolean
  onHideWindowBeforeScreenshotChange?: (checked: boolean) => void
  disabled?: boolean
  placeholder?: string
  canSendFiles?: boolean
  dragActive?: boolean
  mentionCandidates?: MentionCandidate[]
  pendingMention?: MentionCandidate | null
  onMentionConsumed?: () => void
  messages?: Message[]
  sessionName?: string
}

const BUILT_IN_EMOJIS = ['😀', '😁', '😂', '🤣', '😊', '😍', '😘', '😎', '😭', '😡', '👍', '👏', '🙏', '🎉', '❤️', '🔥', '💯', '🌹', '🍉', '☕']
const ALL_MEMBER: MentionCandidate = { id: 'all', nickname: '全体成员' }

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

function formatHistoryTime(ts?: number) {
  if (!Number.isFinite(ts)) return ''
  const d = new Date(ts as number)
  if (!Number.isFinite(d.getTime())) return ''
  return `${(d.getMonth() + 1).toString().padStart(2, '0')}-${d.getDate().toString().padStart(2, '0')} ${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

function messagePreview(message: Message) {
  if (message.type === 'image') return '[图片]'
  if (message.type === 'file') return `[文件] ${message.fileInfo?.name || message.content}`
  return message.content
}

function ToolButton({
  label,
  children,
  onClick,
  disabled,
  withArrow = false,
  className
}: {
  label: string
  children: React.ReactNode
  onClick?: () => void
  disabled?: boolean
  withArrow?: boolean
  className?: string
}) {
  return (
    <button
      type="button"
      aria-label={label}
      title={label}
      onClick={onClick}
      disabled={disabled}
      className={cn(
        'group relative inline-flex h-7 items-center justify-center rounded-md px-1.5 text-[var(--qq-text)] transition-colors hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-primary)] disabled:cursor-not-allowed disabled:opacity-40',
        className
      )}
    >
      {children}
      {withArrow ? <ChevronDown size={12} className="ml-0.5" /> : null}
      <span className="pointer-events-none absolute left-1/2 top-full z-30 mt-1 -translate-x-1/2 whitespace-nowrap rounded bg-black/80 px-2 py-1 text-[11px] text-white opacity-0 shadow-sm transition-opacity group-hover:opacity-100">
        {label}
      </span>
    </button>
  )
}

export function Composer({
  onSend,
  onPickFiles,
  onPickImages,
  onScreenshot,
  screenshotBusy,
  screenshotShortcutLabel = 'Ctrl+Alt+A',
  hideWindowBeforeScreenshot = false,
  onHideWindowBeforeScreenshotChange,
  disabled,
  placeholder = '输入消息...',
  canSendFiles = false,
  dragActive = false,
  mentionCandidates = [],
  pendingMention,
  onMentionConsumed,
  messages = [],
  sessionName = '当前会话'
}: ComposerProps) {
  const [text, setText] = useState('')
  const [cursor, setCursor] = useState(0)
  const [emojiOpen, setEmojiOpen] = useState(false)
  const [historyOpen, setHistoryOpen] = useState(false)
  const [screenshotMenuOpen, setScreenshotMenuOpen] = useState(false)
  const sendingRef = useRef(false)
  const textareaRef = useRef<HTMLTextAreaElement>(null)
  const emojiPanelRef = useRef<HTMLDivElement>(null)
  const historyPanelRef = useRef<HTMLDivElement>(null)
  const screenshotMenuRef = useRef<HTMLDivElement>(null)
  const candidates = useMemo(() => uniqueCandidates([ALL_MEMBER, ...mentionCandidates]), [mentionCandidates])
  const activeToken = mentionToken(text, cursor)
  const filteredCandidates = activeToken
    ? candidates.filter((candidate) => candidate.nickname.toLowerCase().includes(activeToken.query.toLowerCase()))
    : []
  const showMentionPanel = Boolean(activeToken && filteredCandidates.length > 0 && !disabled)

  function focusAt(nextCursor: number) {
    requestAnimationFrame(() => {
      textareaRef.current?.focus()
      textareaRef.current?.setSelectionRange(nextCursor, nextCursor)
    })
  }

  function insertText(value: string) {
    const start = textareaRef.current?.selectionStart ?? cursor
    const end = textareaRef.current?.selectionEnd ?? cursor
    const nextText = `${text.slice(0, start)}${value}${text.slice(end)}`
    const nextCursor = start + value.length
    setText(nextText)
    setCursor(nextCursor)
    focusAt(nextCursor)
  }

  function insertMention(member: MentionCandidate) {
    const mention = `@${member.nickname} `
    const token = mentionToken(text, textareaRef.current?.selectionStart ?? cursor)
    const start = token?.start ?? text.length
    const end = textareaRef.current?.selectionStart ?? cursor
    const nextText = `${text.slice(0, start)}${mention}${text.slice(end)}`
    const nextCursor = start + mention.length
    setText(nextText)
    setCursor(nextCursor)
    focusAt(nextCursor)
  }

  useEffect(() => {
    if (!pendingMention) return
    insertMention(pendingMention)
    onMentionConsumed?.()
  }, [pendingMention])

  useEffect(() => {
    function closePanels(event: MouseEvent) {
      const target = event.target as Node
      if (
        emojiPanelRef.current?.contains(target) ||
        historyPanelRef.current?.contains(target) ||
        screenshotMenuRef.current?.contains(target)
      ) return
      setEmojiOpen(false)
      setHistoryOpen(false)
      setScreenshotMenuOpen(false)
    }
    function closeByKey(event: KeyboardEvent) {
      if (event.key === 'Escape') {
        setEmojiOpen(false)
        setHistoryOpen(false)
        setScreenshotMenuOpen(false)
      }
    }
    document.addEventListener('mousedown', closePanels)
    document.addEventListener('keydown', closeByKey)
    return () => {
      document.removeEventListener('mousedown', closePanels)
      document.removeEventListener('keydown', closeByKey)
    }
  }, [])

  const handleSend = () => {
    const content = text.trim()
    if (!content || disabled || sendingRef.current) return
    sendingRef.current = true
    setText('')
    setCursor(0)
    Promise.resolve(onSend(content)).finally(() => {
      window.setTimeout(() => {
        sendingRef.current = false
      }, 350)
    })
  }

  function startScreenshot() {
    if (disabled || screenshotBusy || !onScreenshot) return
    setScreenshotMenuOpen(false)
    onScreenshot()
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
        'border-t border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] px-3 pb-3 pt-2 transition-colors',
        dragActive && 'bg-[var(--qq-primary-soft)]'
      )}
    >
      <div className="mb-2 flex items-center justify-between text-[var(--qq-text)]">
        <div className="flex items-center gap-1">
          <div className="relative" ref={emojiPanelRef}>
            <ToolButton label="表情" disabled={disabled} onClick={() => setEmojiOpen((open) => !open)}>
              <Smile size={19} />
            </ToolButton>
            {emojiOpen ? (
              <div className="absolute bottom-full left-0 z-40 mb-2 w-72 rounded-xl border border-[var(--qq-border)] bg-white p-3 shadow-[0_14px_40px_rgba(15,23,42,0.16)]">
                <div className="mb-2 flex items-center justify-between">
                  <span className="text-xs font-medium text-[var(--qq-text)]">内置表情</span>
                  <span className="text-[10px] text-[var(--qq-text-tertiary)]">点击插入</span>
                </div>
                <div className="grid grid-cols-8 gap-1">
                  {BUILT_IN_EMOJIS.map((emoji) => (
                    <button
                      key={emoji}
                      type="button"
                      onClick={() => insertText(emoji)}
                      className="flex h-8 items-center justify-center rounded-lg text-lg transition-colors hover:bg-[var(--qq-bg-tertiary)]"
                    >
                      {emoji}
                    </button>
                  ))}
                </div>
              </div>
            ) : null}
          </div>
          <div className="relative" ref={screenshotMenuRef}>
            <div className="inline-flex h-7 items-center rounded-md hover:bg-[var(--qq-bg-tertiary)]">
              <ToolButton
                label={screenshotBusy ? `正在准备截图 · ${screenshotShortcutLabel}` : `截图 · ${screenshotShortcutLabel}`}
                onClick={startScreenshot}
                disabled={disabled || screenshotBusy}
                className="rounded-r-none pr-0.5 hover:bg-transparent"
              >
                {screenshotBusy ? (
                  <span className="h-[18px] w-[18px] animate-spin rounded-full border-2 border-[var(--qq-primary)]/25 border-t-[var(--qq-primary)]" />
                ) : (
                  <Scissors size={18} />
                )}
              </ToolButton>
              <button
                type="button"
                aria-label="截图更多"
                title="截图更多"
                onClick={() => setScreenshotMenuOpen((open) => !open)}
                disabled={disabled || screenshotBusy}
                className="group relative inline-flex h-7 w-3.5 items-center justify-center rounded-r-md text-[var(--qq-text)] hover:text-[var(--qq-primary)] disabled:cursor-not-allowed disabled:opacity-40"
              >
                <ChevronDown size={11} />
              </button>
            </div>
            {screenshotMenuOpen ? (
              <div className="absolute bottom-full left-0 z-50 mb-2 w-[210px] overflow-hidden rounded-md border border-[#dedede] bg-white py-1 text-sm text-[#111] shadow-[0_8px_24px_rgba(0,0,0,0.16)]">
                {[
                  { label: '截图', shortcut: screenshotShortcutLabel, icon: <Scissors size={15} />, action: startScreenshot }
                ].map((item) => (
                  <button
                    key={item.label}
                    type="button"
                    onClick={item.action}
                    className="flex h-[30px] w-full items-center gap-2 px-2 text-left transition-colors hover:bg-[#f1f1f1]"
                  >
                    <span className="flex h-4 w-4 items-center justify-center">{item.icon}</span>
                    <span>{item.label}</span>
                    <span className="ml-auto text-xs text-[#8b8b8b]">{item.shortcut}</span>
                  </button>
                ))}
                <button
                  type="button"
                  onClick={() => onHideWindowBeforeScreenshotChange?.(!hideWindowBeforeScreenshot)}
                  className="mt-1 flex h-[30px] w-full items-center gap-2 border-t border-[#ececec] px-2 text-left transition-colors hover:bg-[#f1f1f1]"
                >
                  <span
                    className={cn(
                      'flex h-4 w-4 items-center justify-center rounded-full border text-[10px]',
                      hideWindowBeforeScreenshot
                        ? 'border-[#0099ff] bg-[#0099ff] text-white'
                        : 'border-[#d5d5d5] bg-white'
                    )}
                  >
                    {hideWindowBeforeScreenshot ? '✓' : ''}
                  </span>
                  <span className="text-xs">隐藏当前窗口</span>
                </button>
              </div>
            ) : null}
          </div>
          <ToolButton label="发送文件" onClick={onPickFiles} disabled={disabled || !canSendFiles} withArrow>
            <Folder size={19} />
          </ToolButton>
          <ToolButton label="发送图片" onClick={onPickImages} disabled={disabled || !canSendFiles}>
            <Image size={19} />
          </ToolButton>
          <span className="sr-only">拖入文件发送</span>
        </div>
        <div className="relative" ref={historyPanelRef}>
          <ToolButton label="聊天记录" onClick={() => setHistoryOpen((open) => !open)} disabled={disabled}>
            <Clock3 size={18} />
          </ToolButton>
          {historyOpen ? (
            <div className="absolute bottom-full right-0 z-40 mb-2 flex h-[420px] w-[360px] flex-col overflow-hidden rounded-xl border border-[var(--qq-border)] bg-white shadow-[0_18px_48px_rgba(15,23,42,0.18)]">
              <div className="border-b border-[var(--qq-border)] px-4 py-3">
                <div className="text-sm font-semibold text-[var(--qq-text)]">聊天记录</div>
                <div className="mt-0.5 text-xs text-[var(--qq-text-tertiary)]">{sessionName} · 共 {messages.length} 条</div>
              </div>
              <div className="flex-1 overflow-auto p-3">
                {messages.length === 0 ? (
                  <div className="flex h-full items-center justify-center text-xs text-[var(--qq-text-tertiary)]">暂无聊天记录</div>
                ) : (
                  messages.map((message) => (
                    <div key={message.id} className="mb-2 rounded-lg bg-[var(--qq-bg-secondary)] px-3 py-2">
                      <div className="mb-1 flex items-center justify-between gap-3 text-[11px] text-[var(--qq-text-tertiary)]">
                        <span className="truncate">{message.senderName}</span>
                        <span className="shrink-0">{formatHistoryTime(message.timestamp)}</span>
                      </div>
                      <p className="line-clamp-3 whitespace-pre-wrap break-words text-xs text-[var(--qq-text)]">{messagePreview(message)}</p>
                    </div>
                  ))
                )}
              </div>
            </div>
          ) : null}
        </div>
      </div>

      <div className="relative">
        {showMentionPanel ? (
          <div className="absolute bottom-full left-0 z-20 mb-2 max-h-60 w-64 overflow-auto rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-2 shadow-[var(--qq-shadow)]">
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
                {candidate.id === 'all' ? <span className="text-[10px] text-[var(--qq-text-tertiary)]">群公告式提醒</span> : null}
              </button>
            ))}
          </div>
        ) : null}
        <div className="flex min-h-[84px] flex-col rounded-lg bg-[var(--qq-bg)]">
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
            rows={2}
            className="max-h-32 min-h-[52px] flex-1 resize-none bg-transparent px-3 py-2 text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
            disabled={disabled}
          />
          <div className="flex justify-end px-2 pb-2">
            <button
              type="button"
              onClick={handleSend}
              disabled={!text.trim() || disabled || sendingRef.current}
              className="inline-flex items-center gap-1 rounded-lg bg-[var(--qq-primary)] px-4 py-1.5 text-sm font-medium text-white transition-opacity hover:opacity-90 disabled:cursor-not-allowed disabled:opacity-40"
            >
              <span>发送</span>
              <span className="h-4 w-px bg-white/30" />
              <ChevronDown size={14} />
              <Send size={0} className="hidden" />
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}

