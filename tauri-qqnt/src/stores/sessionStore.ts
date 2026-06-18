import { create } from 'zustand'
import type { Session } from '@/types/qqnt'

interface SessionState {
  sessions: Session[]
  activeSessionId: string | null
  setSessions: (sessions: Session[]) => void
  setActiveSession: (id: string | null) => void
  updateSession: (id: string, patch: Partial<Session>) => void
  upsertSession: (session: Session) => void
  markRead: (id: string) => void
}

function sortSessions(sessions: Session[]) {
  return [...sessions].sort((left, right) => {
    if (left.pinned !== right.pinned) return left.pinned ? -1 : 1
    const leftTime = left.lastTime ?? 0
    const rightTime = right.lastTime ?? 0
    if (leftTime !== rightTime) return rightTime - leftTime
    return left.name.localeCompare(right.name, 'zh-Hans-CN')
  })
}

export const useSessionStore = create<SessionState>((set) => ({
  sessions: [],
  activeSessionId: null,
  setSessions: (sessions) => set({ sessions: sortSessions(sessions) }),
  setActiveSession: (id) => set({ activeSessionId: id }),
  updateSession: (id, patch) =>
    set((state) => ({
      sessions: sortSessions(state.sessions.map((session) => (session.id === id ? { ...session, ...patch } : session)))
    })),
  upsertSession: (session) =>
    set((state) => {
      const exists = state.sessions.some((item) => item.id === session.id)
      const sessions = exists
        ? state.sessions.map((item) => (item.id === session.id ? { ...item, ...session } : item))
        : [...state.sessions, session]
      return { sessions: sortSessions(sessions) }
    }),
  markRead: (id) =>
    set((state) => ({
      sessions: sortSessions(state.sessions.map((session) => (session.id === id ? { ...session, unread: 0 } : session)))
    }))
}))
