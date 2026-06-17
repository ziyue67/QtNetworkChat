import { create } from 'zustand'
import type { Message } from '@/types/qqnt'

interface MessageState {
  messages: Record<string, Message[]>
  addMessage: (sessionId: string, message: Message) => void
  setMessages: (sessionId: string, messages: Message[]) => void
  updateMessageStatus: (sessionId: string, id: string, status: Message['status']) => void
}

export const useMessageStore = create<MessageState>((set) => ({
  messages: {},
  addMessage: (sessionId, message) =>
    set((state) => ({
      messages: {
        ...state.messages,
        [sessionId]: [...(state.messages[sessionId] || []), message]
      }
    })),
  setMessages: (sessionId, messages) =>
    set((state) => ({
      messages: { ...state.messages, [sessionId]: messages }
    })),
  updateMessageStatus: (sessionId, id, status) =>
    set((state) => ({
      messages: {
        ...state.messages,
        [sessionId]: (state.messages[sessionId] || []).map((m) =>
          m.id === id ? { ...m, status } : m
        )
      }
    }))
}))