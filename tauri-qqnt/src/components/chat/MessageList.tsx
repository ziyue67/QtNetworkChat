import { useEffect, useRef, useCallback, useState } from 'react'
import { createPortal } from 'react-dom'
import { VariableSizeList as List } from 'react-window'
import { AutoSizer } from 'react-virtualized-auto-sizer'
import { Trash2 } from 'lucide-react'
import type { Message, User } from '@/types/qqnt'
import { MessageBubble } from './MessageBubble'
import type { MentionCandidate } from './Composer'

interface MessageListProps {
  sessionId?: string
  messages: Message[]
  currentUser: User | null
  members?: MentionCandidate[]
  onMentionUser?: (member: MentionCandidate) => void
  onRetry?: (id: string) => void
  onCancelFile?: (id: string) => void
  onDownloadFile?: (message: Message) => void
  onOpenFolder?: (message: Message) => void
  onPreviewImage?: (message: Message) => void
  onCopyMessage?: (message: Message) => void
  onForwardMessage?: (message: Message) => void
  onFavoriteMessage?: (message: Message) => void
  onMultiSelectMessage?: (message: Message) => void
  onQuoteMessage?: (message: Message) => void
  onSetEssenceMessage?: (message: Message) => void
  onRecallMessage?: (message: Message) => void
  onDeleteMessage?: (message: Message) => void
  onAddEmoji?: (message: Message) => void
  onOpenDirectMessage?: (member: MentionCandidate) => void
  onViewProfile?: (member: MentionCandidate) => void
  onAddFriend?: (member: MentionCandidate) => void
  onEditGroupNickname?: (member: MentionCandidate) => void
  onReportUser?: (member: MentionCandidate) => void
  onBlockUser?: (member: MentionCandidate) => void
  onClearSessionMessages?: (sessionId: string) => void
}

interface BlankMenuState {
  x: number
  y: number
}

const LINE_HEIGHT = 20
const BASE_HEIGHT = 56
const FILE_HEIGHT = 132
const IMAGE_HEIGHT = 284
const MIN_BUBBLE_WIDTH = 180
const BUBBLE_HORIZONTAL_PADDING = 48
const AVERAGE_CHAR_WIDTH = 8

function estimateTextLines(content: string, width: number) {
  const bubbleWidth = Math.max(MIN_BUBBLE_WIDTH, width * 0.6 - BUBBLE_HORIZONTAL_PADDING)
  const charsPerLine = Math.max(10, Math.floor(bubbleWidth / AVERAGE_CHAR_WIDTH))
  return content.split(/\r?\n/).reduce((lines, line) => {
    return lines + Math.max(1, Math.ceil((line || ' ').length / charsPerLine))
  }, 0)
}

function estimateHeight(message: Message | undefined, width: number) {
  if (!message) return BASE_HEIGHT
  if (message.type === 'image') return IMAGE_HEIGHT
  if (message.type === 'file') return FILE_HEIGHT
  const lines = estimateTextLines(message.content || '', width)
  return BASE_HEIGHT + (lines - 1) * LINE_HEIGHT
}

function senderFromMembers(message: Message, members?: MentionCandidate[]) {
  return members?.find((member) => member.id === message.senderId)
}

function clampBlankMenuPosition(x: number, y: number) {
  const width = 184
  const height = 48
  return {
    x: Math.min(x, Math.max(8, window.innerWidth - width - 8)),
    y: Math.min(y, Math.max(8, window.innerHeight - height - 8))
  }
}

function BlankContextMenu({
  x,
  y,
  onClear,
  onClose
}: {
  x: number
  y: number
  onClear: () => void
  onClose: () => void
}) {
  const menuRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    const close = (event: MouseEvent) => {
      if (menuRef.current?.contains(event.target as Node)) return
      onClose()
    }
    const closeByKey = (event: KeyboardEvent) => {
      if (event.key === 'Escape') onClose()
    }
    document.addEventListener('mousedown', close)
    document.addEventListener('keydown', closeByKey)
    return () => {
      document.removeEventListener('mousedown', close)
      document.removeEventListener('keydown', closeByKey)
    }
  }, [onClose])

  return createPortal(
    <div
      className="fixed z-50 w-[176px] max-w-[calc(100vw-16px)] overflow-hidden rounded-md border border-[var(--qq-border)] bg-white py-1 text-sm text-[var(--qq-text)] shadow-[0_10px_30px_rgba(15,23,42,0.16)]"
      ref={menuRef}
      style={{ left: x, top: y }}
      onMouseDown={(event) => event.stopPropagation()}
      onClick={(event) => event.stopPropagation()}
      onContextMenu={(event) => event.preventDefault()}
      role="menu"
    >
      <button
        type="button"
        role="menuitem"
        onMouseDown={(event) => {
          event.preventDefault()
          event.stopPropagation()
        }}
        onClick={(event) => {
          event.preventDefault()
          event.stopPropagation()
          onClear()
          onClose()
        }}
        className="flex w-full items-center gap-2 px-3 py-1.5 text-left transition-colors hover:bg-[var(--qq-bg-tertiary)]"
      >
        <Trash2 size={15} className="text-[var(--qq-text-secondary)]" />
        清除当前聊天
      </button>

    </div>,
    document.body
  )
}

function MessageItem({
  index,
  style,
  data
}: {
  index: number
  style: React.CSSProperties
  data: MessageListProps
}) {
  const msg = data.messages[index]
  const sender = senderFromMembers(msg, data.members)
  const isSelf = msg.senderId === data.currentUser?.id
  return (
    <div style={{ ...style, width: '100%', overflowX: 'hidden' }}>
      <MessageBubble
        message={msg}
        isSelf={isSelf}
        senderAvatar={sender?.avatar}
        onMentionSender={data.onMentionUser}
        onRetry={data.onRetry}
        onCancelFile={data.onCancelFile}
        onDownloadFile={data.onDownloadFile}
        onOpenFolder={data.onOpenFolder}
        onPreviewImage={data.onPreviewImage}
        onCopyMessage={data.onCopyMessage}
        onForwardMessage={data.onForwardMessage}
        onFavoriteMessage={data.onFavoriteMessage}
        onMultiSelectMessage={data.onMultiSelectMessage}
        onQuoteMessage={data.onQuoteMessage}
        onSetEssenceMessage={data.onSetEssenceMessage}
        onRecallMessage={data.onRecallMessage}
        onDeleteMessage={data.onDeleteMessage}
        onAddEmoji={data.onAddEmoji}
        onOpenDirectMessage={data.onOpenDirectMessage}
        onViewProfile={data.onViewProfile}
        onAddFriend={data.onAddFriend}
        onEditGroupNickname={data.onEditGroupNickname}
        onReportUser={data.onReportUser}
        onBlockUser={data.onBlockUser}
      />
    </div>
  )
}

export function MessageList(props: MessageListProps) {
  const [blankMenu, setBlankMenu] = useState<BlankMenuState | null>(null)
  const sessionId = props.sessionId || props.messages[0]?.sessionId || null

  useEffect(() => {
    setBlankMenu(null)
  }, [sessionId])

  return (
    <div
      className="min-w-0 max-w-full flex-1 overflow-hidden overflow-x-hidden"
      onContextMenu={(event) => {
        if ((event.target as HTMLElement).closest('[data-chat-context-target="true"]')) return
        event.preventDefault()
        event.stopPropagation()
        setBlankMenu(clampBlankMenuPosition(event.clientX, event.clientY))
      }}
    >
      <AutoSizer
        renderProp={({ height, width }: { height: number | undefined; width: number | undefined }) => {
          if (!height || !width) return null
          return (
            <VirtualizedMessages
              {...props}
              messages={props.messages}
              height={height}
              width={width}
            />
          )
        }}
      />
      {blankMenu ? (
        <BlankContextMenu
          x={blankMenu.x}
          y={blankMenu.y}
          onClear={() => sessionId && props.onClearSessionMessages?.(sessionId)}
          onClose={() => setBlankMenu(null)}
        />
      ) : null}
    </div>
  )
}

function VirtualizedMessages(props: MessageListProps & { height: number; width: number }) {
  const { height, width, messages } = props
  const listRef = useRef<List>(null)
  const sizeMap = useRef<Record<number, number>>({})

  const getItemSize = useCallback(
    (index: number) => {
      const cached = sizeMap.current[index]
      if (cached) return cached
      const itemHeight = estimateHeight(messages[index], width)
      sizeMap.current[index] = itemHeight
      return itemHeight
    },
    [messages, width]
  )

  useEffect(() => {
    sizeMap.current = {}
    listRef.current?.resetAfterIndex(0)
    if (messages.length > 0) {
      listRef.current?.scrollToItem(messages.length - 1, 'end')
    }
  }, [messages, width])

  return (
    <List
      ref={listRef}
      height={height}
      width={width}
      style={{ overflowX: 'hidden', maxWidth: '100%' }}
      itemCount={messages.length}
      itemSize={getItemSize}
      itemData={props}
    >
      {MessageItem}
    </List>
  )
}
