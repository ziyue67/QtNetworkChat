import { useEffect, useMemo, useRef, useState } from 'react'
import { createPortal } from 'react-dom'
import {
  Ban,
  Bookmark,
  CheckCircle2,
  Copy,
  CornerUpLeft,
  FileSymlink,
  Flag,
  FolderOpen,
  MessageCircle,
  MessageSquareText,
  PencilLine,
  Quote,
  Save,
  SmilePlus,
  Trash2,
  UserRound,
  UserRoundPlus
} from 'lucide-react'
import { Avatar } from '@/components/common/Avatar'
import { FileMessage } from './FileMessage'
import { cn } from '@/lib/utils'
import type { Message } from '@/types/qqnt'
import type { MentionCandidate } from './Composer'

function formatTime(ts?: number) {
  if (!Number.isFinite(ts)) return ''
  const d = new Date(ts as number)
  if (!Number.isFinite(d.getTime())) return ''
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}`
}

type MenuKind = 'avatar' | 'message' | 'media'

interface ContextMenuState {
  kind: MenuKind
  x: number
  y: number
}

interface MenuItem {
  label: string
  icon: React.ReactNode
  action?: () => void
  danger?: boolean
  dividerBefore?: boolean
}

interface MessageBubbleProps {
  message: Message
  isSelf: boolean
  senderAvatar?: string
  onMentionSender?: (member: MentionCandidate) => void
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
}

function clampMenuPosition(x: number, y: number) {
  const width = 184
  const height = 360
  const maxX = Math.max(8, window.innerWidth - width - 8)
  const maxY = Math.max(8, window.innerHeight - height - 8)
  return { x: Math.max(8, Math.min(x, maxX)), y: Math.max(8, Math.min(y, maxY)) }
}

function ContextMenu({ items, x, y, onClose }: { items: MenuItem[]; x: number; y: number; onClose: () => void }) {
  const menuRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    function close(event: MouseEvent) {
      if (menuRef.current?.contains(event.target as Node)) return
      onClose()
    }
    function closeOnScroll() {
      onClose()
    }
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') onClose()
    }
    document.addEventListener('mousedown', close)
    document.addEventListener('scroll', closeOnScroll, true)
    document.addEventListener('keydown', onKeyDown)
    return () => {
      document.removeEventListener('mousedown', close)
      document.removeEventListener('scroll', closeOnScroll, true)
      document.removeEventListener('keydown', onKeyDown)
    }
  }, [onClose])

  return createPortal(
    <div
      className="fixed z-50 w-[176px] max-w-[calc(100vw-16px)] overflow-hidden rounded-md border border-[var(--qq-border)] bg-white py-1 text-sm text-[var(--qq-text)] shadow-[0_10px_30px_rgba(15,23,42,0.16)]"
      ref={menuRef}
      style={{ left: x, top: y }}
      onContextMenu={(event) => event.preventDefault()}
      onMouseDown={(event) => event.stopPropagation()}
      onClick={(event) => event.stopPropagation()}
      role="menu"
    >
      {items.map((item) => (
        <button
          key={item.label}
          type="button"
          role="menuitem"
          onMouseDown={(event) => {
            event.preventDefault()
            event.stopPropagation()
          }}
          onClick={(event) => {
            event.preventDefault()
            event.stopPropagation()
            item.action?.()
            onClose()
          }}
          className={cn(
            'flex w-full items-center gap-2 px-3 py-1.5 text-left transition-colors hover:bg-[var(--qq-bg-tertiary)]',
            item.dividerBefore && 'mt-1 border-t border-[var(--qq-border)] pt-2',
            item.danger && 'text-[var(--qq-danger)]'
          )}
        >
          <span className="flex h-4 w-4 items-center justify-center text-[var(--qq-text-secondary)]">{item.icon}</span>
          <span>{item.label}</span>
        </button>
      ))}
    </div>,
    document.body
  )
}

export function MessageBubble({
  message,
  isSelf,
  senderAvatar,
  onMentionSender,
  onRetry,
  onCancelFile,
  onDownloadFile,
  onOpenFolder,
  onPreviewImage,
  onCopyMessage,
  onForwardMessage,
  onFavoriteMessage,
  onMultiSelectMessage,
  onQuoteMessage,
  onSetEssenceMessage,
  onRecallMessage,
  onDeleteMessage,
  onAddEmoji,
  onOpenDirectMessage,
  onViewProfile,
  onAddFriend,
  onEditGroupNickname,
  onReportUser,
  onBlockUser
}: MessageBubbleProps) {
  const [menu, setMenu] = useState<ContextMenuState | null>(null)
  const isFile = message.type === 'file' || message.type === 'image'
  const isMedia = message.type === 'image'
  const statusText =
    message.status === 'sending'
      ? '发送中...'
      : message.status === 'failed'
        ? '发送失败（点击重试）'
        : formatTime(message.timestamp)

  const sender = useMemo(
    () => ({ id: message.senderId, nickname: message.senderName, avatar: senderAvatar }),
    [message.senderId, message.senderName, senderAvatar]
  )

  function openMenu(event: React.MouseEvent, kind: MenuKind) {
    event.preventDefault()
    event.stopPropagation()
    const next = clampMenuPosition(event.clientX, event.clientY)
    setMenu({ kind, ...next })
  }

  function avatarItems(): MenuItem[] {
    return [
      { label: '发送消息', icon: <MessageCircle size={15} />, action: () => onOpenDirectMessage?.(sender) },
      { label: `@${sender.nickname}`, icon: <MessageSquareText size={15} />, action: () => onMentionSender?.(sender) },
      { label: '查看资料', icon: <UserRound size={15} />, action: () => onViewProfile?.(sender) },
      { label: '添加好友', icon: <UserRoundPlus size={15} />, action: () => onAddFriend?.(sender) },
      { label: '修改群昵称', icon: <PencilLine size={15} />, action: () => onEditGroupNickname?.(sender) },
      { label: '举报', icon: <Flag size={15} />, action: () => onReportUser?.(sender), dividerBefore: true },
      { label: '屏蔽此人发言', icon: <Ban size={15} />, action: () => onBlockUser?.(sender) }
    ]
  }

  function messageItems(): MenuItem[] {
    return [
      { label: '复制', icon: <Copy size={15} />, action: () => onCopyMessage?.(message) },
      { label: '转发', icon: <FileSymlink size={15} />, action: () => onForwardMessage?.(message) },
      { label: '收藏', icon: <Bookmark size={15} />, action: () => onFavoriteMessage?.(message) },
      { label: '多选', icon: <CheckCircle2 size={15} />, action: () => onMultiSelectMessage?.(message) },
      { label: '引用', icon: <Quote size={15} />, action: () => onQuoteMessage?.(message) },
      { label: '设为精华', icon: <MessageSquareText size={15} />, action: () => onSetEssenceMessage?.(message) },
      { label: '撤回', icon: <CornerUpLeft size={15} />, action: () => onRecallMessage?.(message), dividerBefore: true },
      { label: '删除', icon: <Trash2 size={15} />, action: () => onDeleteMessage?.(message), danger: true }
    ]
  }

  function mediaItems(): MenuItem[] {
    return [
      { label: '添加到表情', icon: <SmilePlus size={15} />, action: () => onAddEmoji?.(message) },
      { label: '复制', icon: <Copy size={15} />, action: () => onCopyMessage?.(message) },
      { label: '转发', icon: <FileSymlink size={15} />, action: () => onForwardMessage?.(message) },
      { label: '收藏', icon: <Bookmark size={15} />, action: () => onFavoriteMessage?.(message) },
      { label: '多选', icon: <CheckCircle2 size={15} />, action: () => onMultiSelectMessage?.(message) },
      { label: '引用', icon: <Quote size={15} />, action: () => onQuoteMessage?.(message) },
      { label: '另存为', icon: <Save size={15} />, action: () => onDownloadFile?.(message) },
      { label: '打开文件夹', icon: <FolderOpen size={15} />, action: () => onOpenFolder?.(message) },
      { label: '设为精华', icon: <MessageSquareText size={15} />, action: () => onSetEssenceMessage?.(message) },
      { label: '删除', icon: <Trash2 size={15} />, action: () => onDeleteMessage?.(message), danger: true, dividerBefore: true }
    ]
  }

  const contextItems = menu?.kind === 'avatar' ? avatarItems() : menu?.kind === 'media' ? mediaItems() : messageItems()

  const avatar = (
    <div
      data-chat-context-target="true"
      onContextMenu={(event) => openMenu(event, 'avatar')}
      onMouseDown={(event) => {
        if (event.button !== 2) event.stopPropagation()
      }}
      title={isSelf ? message.senderName : `右键打开 ${message.senderName} 菜单`}
      className="shrink-0 cursor-context-menu"
    >
      <Avatar src={senderAvatar} fallback={message.senderName} size={36} />
    </div>
  )

  return (
    <div className={cn('mb-4 flex w-full min-w-0 max-w-full items-start overflow-hidden px-4', isSelf ? 'justify-end' : 'justify-start')}>
      {!isSelf ? <div className="mr-3">{avatar}</div> : null}
      <div
        data-chat-context-target="true"
        onClick={() => message.status === 'failed' && onRetry?.(message.id)}
        onContextMenu={(event) => openMenu(event, isMedia ? 'media' : 'message')}
        onMouseDown={(event) => {
          if (event.button !== 2) event.stopPropagation()
        }}
        className={cn(
          'min-w-0 max-w-[calc(100%-64px)] overflow-hidden rounded-lg text-sm shadow-sm',
          isMedia ? 'px-1 py-1 sm:max-w-[72%]' : 'px-3 py-2 sm:max-w-[60%]',
          isSelf ? 'bg-[var(--qq-primary)] text-white' : 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-text)]',
          message.status === 'failed' ? 'cursor-pointer hover:opacity-90' : 'cursor-context-menu'
        )}
      >
        {message.localBlocked ? (
          <p className="text-[var(--qq-text-secondary)]">已屏蔽此人发言</p>
        ) : isFile ? (
          <FileMessage
            message={message}
            isSelf={isSelf}
            onCancel={onCancelFile}
            onDownload={onDownloadFile}
            onOpenFolder={onOpenFolder}
            onPreviewImage={onPreviewImage}
          />
        ) : (
          <p className="whitespace-pre-wrap break-words">{message.content}</p>
        )}
        {(message.localFavorite || message.localSelected || message.localQuote || message.localEssence || message.localEmoji || message.localBlocked) ? (
          <div className={cn('mt-1 flex flex-wrap gap-1 text-[10px]', isSelf ? 'text-white/80' : 'text-[var(--qq-text-secondary)]')}>
            {message.localFavorite ? <span className="rounded bg-black/5 px-1.5 py-0.5">已收藏</span> : null}
            {message.localEmoji ? <span className="rounded bg-black/5 px-1.5 py-0.5">已加表情</span> : null}
            {message.localSelected ? <span className="rounded bg-black/5 px-1.5 py-0.5">已多选</span> : null}
            {message.localQuote ? <span className="rounded bg-black/5 px-1.5 py-0.5">已引用</span> : null}
            {message.localEssence ? <span className="rounded bg-black/5 px-1.5 py-0.5">精华</span> : null}
            {message.localBlocked ? <span className="rounded bg-black/5 px-1.5 py-0.5">已屏蔽</span> : null}
          </div>
        ) : null}
        <span className="mt-1 block text-[10px] opacity-70">{statusText}</span>
      </div>
      {isSelf ? <div className="ml-3">{avatar}</div> : null}
      {menu ? <ContextMenu items={contextItems} x={menu.x} y={menu.y} onClose={() => setMenu(null)} /> : null}
    </div>
  )
}
