import { beforeEach, describe, expect, it } from 'vitest'
import { useSessionStore } from './sessionStore'
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
    expect(useSessionStore.getState().sessions[0].unread).toBe(0)
  })

  it('updates session metadata without changing other sessions', () => {
    useSessionStore.getState().setSessions(sessions)
    useSessionStore.getState().updateSession('10001', { lastMessage: 'new', unread: 1 })

    expect(useSessionStore.getState().sessions[0]).toMatchObject({ lastMessage: 'new', unread: 1 })
    expect(useSessionStore.getState().sessions[1].name).toBe('前端小队')
  })
})
