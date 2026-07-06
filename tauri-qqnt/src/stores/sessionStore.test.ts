import { beforeEach, describe, expect, it } from 'vitest'
import { PUBLIC_SESSION_ID, useSessionStore } from './sessionStore'
import type { Session } from '@/types/qqnt'

const sessions: Session[] = [
  {
    id: '10001',
    type: 'private',
    name: 'Alice',
    lastMessage: 'hello',
    lastTime: 1710000000000,
    unread: 3,
    pinned: false
  },
  {
    id: 'g-100',
    type: 'group',
    name: '前端小队',
    unread: 0,
    pinned: true
  }
]

describe('sessionStore', () => {
  beforeEach(() => {
    useSessionStore.setState({ sessions: [], activeSessionId: null })
  })

  it('sets active session and marks unread as read', () => {
    useSessionStore.getState().setSessions(sessions)
    useSessionStore.getState().setActiveSession('10001')
    useSessionStore.getState().markRead('10001')

    expect(useSessionStore.getState().activeSessionId).toBe('10001')
    expect(useSessionStore.getState().sessions.find((session) => session.id === '10001')?.unread).toBe(0)
  })

  it('updates session metadata without changing other sessions', () => {
    useSessionStore.getState().setSessions(sessions)
    useSessionStore.getState().updateSession('10001', { lastMessage: 'new', unread: 1 })

    expect(useSessionStore.getState().sessions.find((session) => session.id === '10001')).toMatchObject({
      lastMessage: 'new',
      unread: 1
    })
    expect(useSessionStore.getState().sessions.find((session) => session.id === 'g-100')?.name).toBe('前端小队')
  })

  it('sorts pinned sessions before newest regular sessions', () => {
    useSessionStore.getState().setSessions([
      { ...sessions[0], lastTime: 300 },
      { ...sessions[1], lastTime: 100 },
      { id: '10002', type: 'private', name: 'Bob', lastTime: 500, unread: 0, pinned: false }
    ])

    expect(useSessionStore.getState().sessions.map((session) => session.id)).toEqual(['g-100', '10002', '10001'])
  })
  it('normalizes public room aliases into one group session', () => {
    useSessionStore.getState().setSessions([
      { id: 'public', type: 'group', name: '公共聊天室', unread: 0, pinned: false },
      { id: 'group:public', type: 'private', name: 'group:public', unread: 2, pinned: false, lastMessage: 'hi' }
    ])

    expect(useSessionStore.getState().sessions).toHaveLength(1)
    expect(useSessionStore.getState().sessions[0]).toMatchObject({
      id: PUBLIC_SESSION_ID,
      type: 'group',
      name: '公共聊天室'
    })

    useSessionStore.getState().upsertSession({
      id: 'group:public',
      type: 'private',
      name: 'group:public',
      unread: 1,
      pinned: false,
      lastMessage: 'new'
    })

    const publicSessions = useSessionStore.getState().sessions.filter((session) => session.id === PUBLIC_SESSION_ID)
    expect(publicSessions).toHaveLength(1)
    expect(publicSessions[0]).toMatchObject({ type: 'group', name: '公共聊天室', lastMessage: 'new' })
  })
})


