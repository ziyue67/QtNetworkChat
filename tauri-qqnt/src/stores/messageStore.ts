import { create } from 'zustand'
import type { FileInfo, Message } from '@/types/qqnt'

interface MessageState {
  messages: Record<string, Message[]>
  clearedSessionWatermarks: Record<string, number>
  deletedMessageIds: Record<string, string[]>
  blockedMemberIds: Record<string, string[]>
  memberNicknames: Record<string, Record<string, string>>
  addMessage: (sessionId: string, message: Message) => void
  setMessages: (sessionId: string, messages: Message[]) => void
  updateMessageStatus: (sessionId: string, id: string, status: Message['status']) => void
  patchMessage: (sessionId: string, id: string, patch: Partial<Message>) => void
  updateFileMessage: (transferId: string, patch: Partial<FileInfo>, status?: Message['status']) => void
  removeMessage: (sessionId: string, id: string) => void
  clearSessionMessages: (sessionId: string) => void
  blockMemberMessages: (sessionId: string, memberId: string) => void
  renameMemberMessages: (sessionId: string, memberId: string, nickname: string) => void
  applyLocalChatActions: (actions: LocalChatActionsLike) => void
}

interface LocalMessageActionLike {
  sessionId: string
  messageId: string
  value?: unknown
}

interface LocalChatActionsLike {
  clearedSessions?: Array<{ sessionId: string; clearedAt: number }>
  deletedMessages?: LocalMessageActionLike[]
  favoriteMessages?: LocalMessageActionLike[]
  emojiMessages?: LocalMessageActionLike[]
  selectedMessages?: LocalMessageActionLike[]
  quoteMessages?: LocalMessageActionLike[]
  essenceMessages?: LocalMessageActionLike[]
  recalledMessages?: LocalMessageActionLike[]
  memberActions?: LocalMemberActionLike[]
}

interface LocalMemberActionLike {
  kind: string
  memberId: string
  sessionId?: string
  value?: unknown
}

function normalizeSessionKey(sessionId: string) {
  return sessionId === 'group:public' || sessionId === 'public' ? 'public' : sessionId
}

function withNormalizedSession(message: Message, sessionId: string): Message {
  const normalizedSessionId = normalizeSessionKey(message.sessionId || sessionId)
  return normalizedSessionId === message.sessionId ? message : { ...message, sessionId: normalizedSessionId }
}

function isClientOriginatedMessage(message: Message) {
  return message.id.startsWith('client-') || Boolean(message.clientMessageId?.startsWith('client-'))
}

function normalizedMessageContent(content: string) {
  return content.trim()
}

function comparableTimestamp(timestamp?: number) {
  if (!timestamp) return 0
  return timestamp < 100000000000 ? timestamp * 1000 : timestamp
}

function sameRecentOutgoingEcho(left: Message, right: Message) {
  if (!isClientOriginatedMessage(left) || isClientOriginatedMessage(right)) return false
  if (normalizeSessionKey(left.sessionId) !== normalizeSessionKey(right.sessionId)) return false
  if (left.type !== right.type) return false
  if (normalizedMessageContent(left.content) !== normalizedMessageContent(right.content)) return false
  const senderMatches = left.senderId === right.senderId
  const looksLikeOwnServerEcho = right.status === 'sent' || right.status === 'read'
  const leftTimestamp = comparableTimestamp(left.timestamp)
  const rightTimestamp = comparableTimestamp(right.timestamp)
  if (!leftTimestamp || !rightTimestamp) return true
  const delta = Math.abs(leftTimestamp - rightTimestamp)
  if (senderMatches || looksLikeOwnServerEcho) return delta <= 60000
  return delta <= 10000
}

function sameRecentOutgoingContent(left: Message, right: Message) {
  if (isClientOriginatedMessage(left) && isClientOriginatedMessage(right)) return false
  if (normalizeSessionKey(left.sessionId) !== normalizeSessionKey(right.sessionId)) return false
  if (left.type !== right.type) return false
  if (normalizedMessageContent(left.content) !== normalizedMessageContent(right.content)) return false
  if (left.senderId && right.senderId && left.senderId !== right.senderId) return false
  const leftTimestamp = comparableTimestamp(left.timestamp)
  const rightTimestamp = comparableTimestamp(right.timestamp)
  if (!leftTimestamp || !rightTimestamp) return false
  return Math.abs(leftTimestamp - rightTimestamp) <= 2500
}

function sameMessage(left: Message, right: Message) {
  return Boolean(
    (left.id && left.id === right.id) ||
      (left.messageId && left.messageId === right.messageId) ||
      (left.clientMessageId && left.clientMessageId === right.clientMessageId) ||
      (left.fileInfo?.id && left.fileInfo.id === right.fileInfo?.id) ||
      sameRecentOutgoingEcho(left, right) ||
      sameRecentOutgoingEcho(right, left) ||
      sameRecentOutgoingContent(left, right)
  )
}

function messageHasIdentifier(message: Message, identifier: string) {
  return Boolean(
    message.id === identifier ||
      message.messageId === identifier ||
      message.clientMessageId === identifier ||
      message.fileInfo?.id === identifier
  )
}

function sessionDeletedIds(state: MessageState, sessionId: string) {
  return state.deletedMessageIds[sessionId] || []
}

function isMessageLocallyHidden(state: MessageState, sessionId: string, message: Message) {
  const normalizedSessionId = normalizeSessionKey(sessionId)
  const clearedAt = state.clearedSessionWatermarks[normalizedSessionId] || 0
  if (clearedAt && (message.timestamp || 0) <= clearedAt) return true
  return sessionDeletedIds(state, normalizedSessionId).some((identifier) => messageHasIdentifier(message, identifier))
}

function messageWithLocalMemberState(state: MessageState, sessionId: string, message: Message): Message {
  const normalizedSessionId = normalizeSessionKey(sessionId)
  const blocked = state.blockedMemberIds[normalizedSessionId]?.includes(message.senderId)
  const nickname = state.memberNicknames[normalizedSessionId]?.[message.senderId]
  if (!blocked && !nickname) return message
  return {
    ...message,
    senderName: nickname || message.senderName,
    localBlocked: message.localBlocked || blocked
  }
}

function messageMatchesFileUpdate(message: Message, identifier: string, patch: Partial<FileInfo>) {
  if (messageHasIdentifier(message, identifier)) return true
  if (!message.fileInfo || message.status !== 'sending') return false
  if (patch.path && message.fileInfo.path === patch.path) return true
  return Boolean(patch.name && message.fileInfo.name === patch.name)
}

function mergeFileInfo(message: Message, transferId: string, patch: Partial<FileInfo>) {
  if (!message.fileInfo) return undefined
  const nextPatch = { ...patch }
  if (nextPatch.mime === 'application/octet-stream' && message.fileInfo.mime.startsWith('image/')) {
    delete nextPatch.mime
  }
  if (nextPatch.path && message.fileInfo.path && message.fileInfo.progress >= 100) {
    delete nextPatch.path
  }
  return { ...message.fileInfo, ...nextPatch, id: nextPatch.id ?? transferId ?? message.fileInfo.id }
}

function dedupeMessages(messages: Message[]) {
  return messages.reduce<Message[]>((acc, message) => {
    const index = acc.findIndex((existing) => sameMessage(existing, message))
    if (index >= 0) {
      const existing = acc[index]
      const fileInfo = existing.fileInfo || message.fileInfo
      acc[index] = {
        ...message,
        clientMessageId: existing.clientMessageId || message.clientMessageId,
        localFavorite: existing.localFavorite || message.localFavorite,
        localEmoji: existing.localEmoji || message.localEmoji,
        localSelected: existing.localSelected || message.localSelected,
        localQuote: existing.localQuote || message.localQuote,
        localEssence: existing.localEssence || message.localEssence,
        localRecalled: existing.localRecalled || message.localRecalled,
        fileInfo: fileInfo
          ? { ...fileInfo, ...existing.fileInfo, ...message.fileInfo, id: message.fileInfo?.id || existing.fileInfo?.id || message.id }
          : undefined
      }
      return acc
    }
    acc.push(message)
    return acc
  }, [])
}

function localActionMatchesMessage(action: LocalMessageActionLike, message: Message) {
  return messageHasIdentifier(message, action.messageId)
}

function quoteLabelFromAction(action: LocalMessageActionLike) {
  const value = action.value as Partial<Message> | undefined
  if (!value || typeof value !== 'object') return '已引用'
  return value.content || value.fileInfo?.name || value.id || '已引用'
}

export const useMessageStore = create<MessageState>((set) => ({
  messages: {},
  clearedSessionWatermarks: {},
  deletedMessageIds: {},
  blockedMemberIds: {},
  memberNicknames: {},
  addMessage: (sessionId, message) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      const normalizedMessage = messageWithLocalMemberState(state, normalizedSessionId, withNormalizedSession(message, normalizedSessionId))
      if (isMessageLocallyHidden(state, normalizedSessionId, normalizedMessage)) return state
      return {
        messages: {
          ...state.messages,
          [normalizedSessionId]: dedupeMessages([...(state.messages[normalizedSessionId] || []), normalizedMessage])
        }
      }
    }),
  setMessages: (sessionId, messages) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      const normalizedMessages = messages.map((message) =>
        messageWithLocalMemberState(state, normalizedSessionId, withNormalizedSession(message, normalizedSessionId))
      )
      return {
        messages: {
          ...state.messages,
          [normalizedSessionId]: dedupeMessages(
            normalizedMessages.filter((message) => !isMessageLocallyHidden(state, normalizedSessionId, message))
          )
        }
      }
    }),
  updateMessageStatus: (sessionId, id, status) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      return {
        messages: {
          ...state.messages,
          [normalizedSessionId]: (state.messages[normalizedSessionId] || []).map((message) =>
            messageHasIdentifier(message, id) ? { ...message, status } : message
          )
        }
      }
    }),
  patchMessage: (sessionId, id, patch) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      return {
        messages: {
          ...state.messages,
          [normalizedSessionId]: (state.messages[normalizedSessionId] || []).map((message) =>
            messageHasIdentifier(message, id) ? { ...message, ...patch } : message
          )
        }
      }
    }),
  updateFileMessage: (transferId, patch, status) =>
    set((state) => ({
      messages: Object.fromEntries(
        Object.entries(state.messages).map(([sessionId, sessionMessages]) => [
          sessionId,
          sessionMessages.map((message) => {
            if (!messageMatchesFileUpdate(message, transferId, patch) || !message.fileInfo) return message
            return {
              ...message,
              status: status ?? message.status,
              fileInfo: mergeFileInfo(message, transferId, patch)
            }
          })
        ])
      )
    })),
  removeMessage: (sessionId, id) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      return {
        messages: {
          ...state.messages,
          [normalizedSessionId]: (state.messages[normalizedSessionId] || []).filter((message) => !messageHasIdentifier(message, id))
        },
        deletedMessageIds: {
          ...state.deletedMessageIds,
          [normalizedSessionId]: Array.from(new Set([...(state.deletedMessageIds[normalizedSessionId] || []), id]))
        }
      }
    }),
  clearSessionMessages: (sessionId) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      return {
        messages: {
          ...state.messages,
          [normalizedSessionId]: []
        },
        clearedSessionWatermarks: {
          ...state.clearedSessionWatermarks,
          [normalizedSessionId]: Date.now()
        }
      }
    }),
  blockMemberMessages: (sessionId, memberId) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      const blockedMembers = Array.from(new Set([...(state.blockedMemberIds[normalizedSessionId] || []), memberId]))
      return {
        blockedMemberIds: {
          ...state.blockedMemberIds,
          [normalizedSessionId]: blockedMembers
        },
        messages: {
          ...state.messages,
          [normalizedSessionId]: (state.messages[normalizedSessionId] || []).map((message) =>
            message.senderId === memberId ? { ...message, localBlocked: true } : message
          )
        }
      }
    }),
  renameMemberMessages: (sessionId, memberId, nickname) =>
    set((state) => {
      const normalizedSessionId = normalizeSessionKey(sessionId)
      return {
        memberNicknames: {
          ...state.memberNicknames,
          [normalizedSessionId]: {
            ...(state.memberNicknames[normalizedSessionId] || {}),
            [memberId]: nickname
          }
        },
        messages: {
          ...state.messages,
          [normalizedSessionId]: (state.messages[normalizedSessionId] || []).map((message) =>
            message.senderId === memberId ? { ...message, senderName: nickname } : message
          )
        }
      }
    }),
  applyLocalChatActions: (actions) =>
    set((state) => {
      const clearedSessionWatermarks = { ...state.clearedSessionWatermarks }
      for (const item of actions.clearedSessions || []) {
        const sessionId = normalizeSessionKey(item.sessionId)
        clearedSessionWatermarks[sessionId] = Math.max(
          clearedSessionWatermarks[sessionId] || 0,
          item.clearedAt || 0
        )
      }
      const deletedMessageIds = { ...state.deletedMessageIds }
      for (const item of [...(actions.deletedMessages || []), ...(actions.recalledMessages || [])]) {
        const sessionId = normalizeSessionKey(item.sessionId)
        deletedMessageIds[sessionId] = Array.from(
          new Set([...(deletedMessageIds[sessionId] || []), item.messageId])
        )
      }
      const blockedMemberIds = { ...state.blockedMemberIds }
      const memberNicknames = { ...state.memberNicknames }
      for (const item of actions.memberActions || []) {
        const sessionId = normalizeSessionKey(item.sessionId || '')
        if (!sessionId || !item.memberId) continue
        if (item.kind === 'block_user') {
          blockedMemberIds[sessionId] = Array.from(new Set([...(blockedMemberIds[sessionId] || []), item.memberId]))
        }
        if (item.kind === 'edit_group_nickname') {
          const value = item.value as { extra?: { nickname?: string } } | undefined
          const nickname = value?.extra?.nickname?.trim()
          if (nickname) {
            memberNicknames[sessionId] = {
              ...(memberNicknames[sessionId] || {}),
              [item.memberId]: nickname
            }
          }
        }
      }
      const messages = Object.fromEntries(
        Object.entries(state.messages).map(([sessionId, sessionMessages]) => [
          sessionId,
          sessionMessages
            .filter((message) => {
              const clearedAt = clearedSessionWatermarks[sessionId] || 0
              if (clearedAt && (message.timestamp || 0) <= clearedAt) return false
              return !(deletedMessageIds[sessionId] || []).some((identifier) => messageHasIdentifier(message, identifier))
            })
            .map((message) => {
              const normalizedSessionId = normalizeSessionKey(message.sessionId || sessionId)
              const actionsFor = (items?: LocalMessageActionLike[]) =>
                (items || []).filter((item) => normalizeSessionKey(item.sessionId) === normalizedSessionId)
              const favorite = actionsFor(actions.favoriteMessages).some((item) => localActionMatchesMessage(item, message))
              const emoji = actionsFor(actions.emojiMessages).some((item) => localActionMatchesMessage(item, message))
              const selected = actionsFor(actions.selectedMessages).some((item) => localActionMatchesMessage(item, message))
              const essence = actionsFor(actions.essenceMessages).some((item) => localActionMatchesMessage(item, message))
              const quote = actionsFor(actions.quoteMessages).find((item) => localActionMatchesMessage(item, message))
              return {
                ...message,
                senderName: memberNicknames[normalizedSessionId]?.[message.senderId] || message.senderName,
                localFavorite: message.localFavorite || favorite,
                localEmoji: message.localEmoji || emoji,
                localSelected: message.localSelected || selected,
                localQuote: message.localQuote || (quote ? quoteLabelFromAction(quote) : undefined),
                localEssence: message.localEssence || essence,
                localBlocked: message.localBlocked || blockedMemberIds[normalizedSessionId]?.includes(message.senderId)
              }
            })
        ])
      )
      return { messages, clearedSessionWatermarks, deletedMessageIds, blockedMemberIds, memberNicknames }
    })
}))
