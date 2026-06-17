import { create } from 'zustand'
import type { Message } from '@/types/qqnt'

interface MessageState {
  messages: Record<string, Message[]>
  addMessage: (sessionId: string, message: Message) => void
  setMessages: (sessionId: string, messages: Message[]) => void
  updateMessageStatus: (sessionId: string, id: string, status: Message['status']) => void
}

function sameMessage(left: Message, right: Message) {
  return Boolean(
    (left.id && left.id === right.id) ||
      (left.messageId && left.messageId === right.messageId) ||
      (left.clientMessageId && left.clientMessageId === right.clientMessageId) ||
      (left.fileInfo?.id && left.fileInfo.id === right.fileInfo?.id)
  )
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
          message.id === id ? { ...message, status } : message
        )
      }
    }))
}))
