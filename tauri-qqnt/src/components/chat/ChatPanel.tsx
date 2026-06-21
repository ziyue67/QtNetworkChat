import { useState } from 'react'
import type { Session, Message, User } from '@/types/qqnt'
import { MessageList } from './MessageList'
import { Composer, type MentionCandidate } from './Composer'

interface ChatPanelProps {
  session: Session
  messages: Message[]
  currentUser: User | null
  members?: MentionCandidate[]
  onSend: (content: string) => void
  onPickFiles?: () => void
  onPickImages?: () => void
  onScreenshot?: () => void
  screenshotBusy?: boolean
  screenshotShortcutLabel?: string
  hideWindowBeforeScreenshot?: boolean
  onHideWindowBeforeScreenshotChange?: (checked: boolean) => void
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
  loading?: boolean
  dragActive?: boolean
  attachmentError?: string
  actionNotice?: string
  peerStatus?: User['status']
}

const STATUS_LABEL: Record<User['status'], string> = {
  online: '在线',
  offline: '离线',
  busy: '忙碌',
  away: '离开'
}

export function ChatPanel({
  session,
  messages,
  currentUser,
  members = [],
  onSend,
  onPickFiles,
  onPickImages,
  onScreenshot,
  screenshotBusy,
  screenshotShortcutLabel,
  hideWindowBeforeScreenshot,
  onHideWindowBeforeScreenshotChange,
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
  onBlockUser,
  onClearSessionMessages,
  loading,
  dragActive,
  attachmentError,
  actionNotice,
  peerStatus
}: ChatPanelProps) {
  const [pendingMention, setPendingMention] = useState<MentionCandidate | null>(null)

  return (
    <main className="relative flex min-w-0 flex-1 flex-col">
      <div className="flex h-[var(--qq-titlebar-height)] items-center justify-between border-b border-[var(--qq-border)] px-4">
        <div>
          <span className="text-sm font-semibold text-[var(--qq-text)]">{session.name}</span>
          {peerStatus ? (
            <span className="ml-2 text-xs text-[var(--qq-text-tertiary)]">{STATUS_LABEL[peerStatus]}</span>
          ) : null}
        </div>
        {peerStatus === 'online' ? (
          <span className="rounded-full bg-[var(--qq-success)]/10 px-2 py-0.5 text-xs text-[var(--qq-success)]">在线</span>
        ) : null}
      </div>
      {dragActive ? (
        <div className="pointer-events-none absolute inset-x-4 bottom-24 top-14 z-10 flex items-center justify-center rounded-2xl border-2 border-dashed border-[var(--qq-primary)] bg-[var(--qq-primary-soft)] text-sm font-medium text-[var(--qq-primary)]">
          松开鼠标发送文件或图片
        </div>
      ) : null}
      <MessageList
        sessionId={session.id}
        messages={messages}
        currentUser={currentUser}
        members={members}
        onMentionUser={setPendingMention}
        onRetry={onRetry}
        onCancelFile={onCancelFile}
        onDownloadFile={onDownloadFile}
        onOpenFolder={onOpenFolder}
        onPreviewImage={onPreviewImage}
        onCopyMessage={onCopyMessage}
        onForwardMessage={onForwardMessage}
        onFavoriteMessage={onFavoriteMessage}
        onMultiSelectMessage={onMultiSelectMessage}
        onQuoteMessage={onQuoteMessage}
        onSetEssenceMessage={onSetEssenceMessage}
        onRecallMessage={onRecallMessage}
        onDeleteMessage={onDeleteMessage}
        onAddEmoji={onAddEmoji}
        onOpenDirectMessage={onOpenDirectMessage}
        onViewProfile={onViewProfile}
        onAddFriend={onAddFriend}
        onEditGroupNickname={onEditGroupNickname}
        onReportUser={onReportUser}
        onBlockUser={onBlockUser}
        onClearSessionMessages={onClearSessionMessages}
      />
      <Composer
        onSend={onSend}
        onPickFiles={onPickFiles}
        onPickImages={onPickImages}
        onScreenshot={onScreenshot}
        screenshotBusy={screenshotBusy}
        screenshotShortcutLabel={screenshotShortcutLabel}
        hideWindowBeforeScreenshot={hideWindowBeforeScreenshot}
        onHideWindowBeforeScreenshotChange={onHideWindowBeforeScreenshotChange}
        disabled={loading || !currentUser}
        canSendFiles={Boolean(currentUser)}
        dragActive={dragActive}
        mentionCandidates={members.filter((member) => member.id !== currentUser?.id)}
        pendingMention={pendingMention}
        onMentionConsumed={() => setPendingMention(null)}
        messages={messages}
        sessionName={session.name}
      />
      {attachmentError ? (
        <div className="border-t border-[var(--qq-border)] bg-[var(--qq-danger)]/10 px-4 py-2 text-xs text-[var(--qq-danger)]">
          {attachmentError}
        </div>
      ) : null}
      {actionNotice ? (
        <div className="border-t border-[var(--qq-border)] bg-[#eef7ff] px-4 py-2 text-xs text-[#1677ff]">
          {actionNotice}
        </div>
      ) : null}
    </main>
  )
}
