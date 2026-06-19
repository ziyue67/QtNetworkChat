import { create } from 'zustand'
import type { FileInfo, Message } from '@/types/qqnt'

interface MessageState {
  messages: Record<string, Message[]>
  addMessage: (sessionId: string, message: Message) => void
  setMessages: (sessionId: string, messages: Message[]) => void
  updateMessageStatus: (sessionId: string, id: string, status: Message['status']) => void
  updateFileMessage: (transferId: string, patch: Partial<FileInfo>, status?: Message['status']) => void
}

function sameMessage(left: Message, right: Message) {
  return Boolean(
    (left.id && left.id === right.id) ||
      (left.messageId && left.messageId === right.messageId) ||
      (left.clientMessageId && left.clientMessageId === right.clientMessageId) ||
      (left.fileInfo?.id && left.fileInfo.id === right.fileInfo?.id)
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

function messageMatchesFileUpdate(message: Message, identifier: string, patch: Partial<FileInfo>) {
  if (messageHasIdentifier(message, identifier)) return true
  if (!message.fileInfo || message.status !== 'sending') return false
  if (patch.path && message.fileInfo.path === patch.path) return true
  return Boolean(patch.name && message.fileInfo.name === patch.name)
}

function dedupeMessages(messages: Message[]) {
  return messages.reduce<Message[]>((acc, message) => {
    const index = acc.findIndex((existing) => sameMessage(existing, message))
    if (index >= 0) {
      const existing = acc[index]
      const fileInfo = existing.fileInfo || message.fileInfo
      acc[index] = {
        ...existing,
        ...message,
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

export const useMessageStore = create<MessageState>((set) => ({
  messages: {},
  addMessage: (sessionId, message) =>
    set((state) => ({
      messages: {
        ...state.messages,
        [sessionId]: dedupeMessages([...(state.messages[sessionId] || []), message])
      }
    })),
  setMessages: (sessionId, messages) =>
    set((state) => ({
      messages: { ...state.messages, [sessionId]: dedupeMessages(messages) }
    })),
  updateMessageStatus: (sessionId, id, status) =>
    set((state) => ({
      messages: {
        ...state.messages,
        [sessionId]: (state.messages[sessionId] || []).map((message) =>
          messageHasIdentifier(message, id) ? { ...message, status } : message
        )
      }
    })),
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
              fileInfo: { ...message.fileInfo, ...patch, id: patch.id ?? transferId ?? message.fileInfo.id }
            }
          })
        ])
      )
    }))
}))
