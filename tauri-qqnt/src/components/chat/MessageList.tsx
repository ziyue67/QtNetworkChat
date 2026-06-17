import { useEffect, useRef, useCallback } from 'react'
import { VariableSizeList as List } from 'react-window'
import { AutoSizer } from 'react-virtualized-auto-sizer'
import type { Message, User } from '@/types/qqnt'
import { MessageBubble } from './MessageBubble'

interface MessageListProps {
  messages: Message[]
  currentUser: User | null
  onRetry?: (id: string) => void
}

const LINE_HEIGHT = 20
const BASE_HEIGHT = 56
const CHARS_PER_LINE = 28

function estimateHeight(content: string) {
  const lines = Math.max(1, Math.ceil((content || '').length / CHARS_PER_LINE))
  return BASE_HEIGHT + (lines - 1) * LINE_HEIGHT
}

function MessageItem({
  index,
  style,
  data
}: {
  index: number
  style: React.CSSProperties
  data: { messages: Message[]; currentUser: User | null; onRetry?: (id: string) => void }
}) {
  const msg = data.messages[index]
  return (
    <div style={style}>
      <MessageBubble message={msg} isSelf={msg.senderId === data.currentUser?.id} onRetry={data.onRetry} />
    </div>
  )
}

export function MessageList({ messages, currentUser, onRetry }: MessageListProps) {
  const listRef = useRef<List>(null)
  const sizeMap = useRef<Record<number, number>>({})

  const getItemSize = useCallback(
    (index: number) => {
      const cached = sizeMap.current[index]
      if (cached) return cached
      const height = estimateHeight(messages[index]?.content || '')
      sizeMap.current[index] = height
      return height
    },
    [messages]
  )

  useEffect(() => {
    sizeMap.current = {}
    listRef.current?.resetAfterIndex(0)
    if (messages.length > 0) {
      listRef.current?.scrollToItem(messages.length - 1, 'end')
    }
  }, [messages])

  return (
    <div className="flex-1 overflow-hidden">
      <AutoSizer
        renderProp={({ height, width }: { height: number | undefined; width: number | undefined }) => {
          if (!height || !width) return null
          return (
            <List
              ref={listRef}
              height={height}
              width={width}
              itemCount={messages.length}
              itemSize={getItemSize}
              itemData={{ messages, currentUser, onRetry }}
            >
              {MessageItem}
            </List>
          )
        }}
      />
    </div>
  )
}
