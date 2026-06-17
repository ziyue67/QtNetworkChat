import { create } from 'zustand'
import type { Session } from '@/types/qqnt'

interface SessionState {
  sessions: Session[]
  activeSessionId: string | null
  setSessions: (sessions: Session[]) => void
  setActiveSession: (id: string | null) => void
  updateSession: (id: string, patch: Partial<Session>) => void
  markRead: (id: string) => void
}

export const useSessionStore = create<SessionState>((set) => ({
  sessions: [],
  activeSessionId: null,
  setSessions: (sessions) => set({ sessions }),
  setActiveSession: (id) => set({ activeSessionId: id }),
  updateSession: (id, patch) =>
    set((state) => ({
      sessions: state.sessions.map((s) => (s.id === id ? { ...s, ...patch } : s))
    })),
  markRead: (id) =>
    set((state) => ({
      sessions: state.sessions.map((s) => (s.id === id ? { ...s, unread: 0 } : s))
    }))
}))