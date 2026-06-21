import { create } from 'zustand'
import type { Session } from '@/types/qqnt'

export const PUBLIC_SESSION_ID = 'public'
const PUBLIC_SESSION_ALIASES = new Set(['group:public', 'public'])
const PUBLIC_SESSION_NAME = '公共聊天室'

function isPublicSessionId(id?: string | null) {
  return Boolean(id && PUBLIC_SESSION_ALIASES.has(id))
}

export function normalizeSessionId(id: string) {
  return isPublicSessionId(id) ? PUBLIC_SESSION_ID : id
}

export function isGroupSessionId(id: string) {
  const normalizedId = normalizeSessionId(id)
  return normalizedId === PUBLIC_SESSION_ID || normalizedId.startsWith('g-') || normalizedId.startsWith('group:')
}

export function normalizeSession(session: Session): Session {
  if (!isPublicSessionId(session.id)) return session
  return {
    ...session,
    id: PUBLIC_SESSION_ID,
    type: 'group',
    name: PUBLIC_SESSION_NAME,
    avatar: session.avatar,
    unread: session.unread ?? 0,
    pinned: session.pinned ?? false
  }
}

interface SessionState {
  sessions: Session[]
  activeSessionId: string | null
  setSessions: (sessions: Session[]) => void
  setActiveSession: (id: string | null) => void
  updateSession: (id: string, patch: Partial<Session>) => void
  upsertSession: (session: Session) => void
  markRead: (id: string) => void
}


function mergeSessions(sessions: Session[]) {
  const byId = new Map<string, Session>()
  sessions.map(normalizeSession).forEach((session) => {
    const existing = byId.get(session.id)
    if (!existing) {
      byId.set(session.id, session)
      return
    }
    const merged = normalizeSession({ ...existing, ...session })
    const hasLastMessage = Object.prototype.hasOwnProperty.call(session, 'lastMessage')
    const hasLastTime = Object.prototype.hasOwnProperty.call(session, 'lastTime')
    byId.set(session.id, {
      ...merged,
      unread: Math.max(existing.unread ?? 0, session.unread ?? 0),
      lastMessage: hasLastMessage ? session.lastMessage : existing.lastMessage,
      lastTime: hasLastTime ? session.lastTime : Math.max(existing.lastTime ?? 0, session.lastTime ?? 0) || undefined,
      pinned: existing.pinned || session.pinned
    })
  })
  return Array.from(byId.values())
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
  setSessions: (sessions) => set({ sessions: sortSessions(mergeSessions(sessions)) }),
  setActiveSession: (id) => set({ activeSessionId: id ? normalizeSessionId(id) : id }),
  updateSession: (id, patch) =>
    set((state) => {
      const normalizedId = normalizeSessionId(id)
      return {
        sessions: sortSessions(mergeSessions(
          state.sessions.map((session) =>
            normalizeSessionId(session.id) === normalizedId ? normalizeSession({ ...session, ...patch, id: normalizedId }) : normalizeSession(session)
          )
        ))
      }
    }),
  upsertSession: (session) =>
    set((state) => {
      const normalized = normalizeSession(session)
      const exists = state.sessions.some((item) => normalizeSessionId(item.id) === normalized.id)
      const sessions = exists
        ? mergeSessions(state.sessions.map((item) => (normalizeSessionId(item.id) === normalized.id ? normalizeSession({ ...item, ...normalized }) : normalizeSession(item))))
        : mergeSessions([...state.sessions, normalized])
      return { sessions: sortSessions(sessions) }
    }),
  markRead: (id) =>
    set((state) => {
      const normalizedId = normalizeSessionId(id)
      return {
        sessions: sortSessions(mergeSessions(
          state.sessions.map((session) =>
            normalizeSessionId(session.id) === normalizedId ? normalizeSession({ ...session, unread: 0 }) : normalizeSession(session)
          )
        ))
      }
    })
}))




