import { useEffect, useRef, useCallback } from 'react'
import { VariableSizeList as List } from 'react-window'
import { AutoSizer } from 'react-virtualized-auto-sizer'
import type { Message, User } from '@/types/qqnt'
import { MessageBubble } from './MessageBubble'

interface MessageListProps {
  messages: Message[]
  currentUser: User | null
  onRetry?: (id: string) => void
  onCancelFile?: (id: string) => void
  onDownloadFile?: (message: Message) => void
  onOpenFolder?: (message: Message) => void
}

const LINE_HEIGHT = 20
const BASE_HEIGHT = 56
const FILE_HEIGHT = 132
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
  if (message.type === 'file' || message.type === 'image') return FILE_HEIGHT
  const lines = estimateTextLines(message.content || '', width)
  return BASE_HEIGHT + (lines - 1) * LINE_HEIGHT
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
  return (
    <div style={style}>
      <MessageBubble
        message={msg}
        isSelf={msg.senderId === data.currentUser?.id}
        onRetry={data.onRetry}
        onCancelFile={data.onCancelFile}
        onDownloadFile={data.onDownloadFile}
        onOpenFolder={data.onOpenFolder}
      />
    </div>
  )
}

export function MessageList(props: MessageListProps) {
  return (
    <div className="flex-1 overflow-hidden">
      <AutoSizer
        renderProp={({ height, width }: { height: number | undefined; width: number | undefined }) => {
          if (!height || !width) return null
          return (
            <VirtualizedMessages
              {...props}
              height={height}
              width={width}
            />
          )
        }}
      />
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
      itemCount={messages.length}
      itemSize={getItemSize}
      itemData={props}
    >
      {MessageItem}
    </List>
  )
}
