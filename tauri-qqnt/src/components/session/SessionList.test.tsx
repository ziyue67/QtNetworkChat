import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen } from '@testing-library/react'
import { SessionList } from './SessionList'
import type { Session } from '@/types/qqnt'

const sessions: Session[] = [
  {
    id: '10001',
    type: 'private',
    name: 'Alice',
    lastMessage: 'hello',
    lastTime: 1710000000000,
    unread: 7,
    pinned: false
  }
]

describe('SessionList', () => {
  it('renders unread and calls onSelect', () => {
    const onSelect = vi.fn()
    render(<SessionList sessions={sessions} activeSessionId={null} onSelect={onSelect} />)

    expect(screen.getByText('Alice')).toBeTruthy()
    expect(screen.getByText('hello')).toBeTruthy()
    expect(screen.getByText('7')).toBeTruthy()

    fireEvent.click(screen.getByText('Alice'))
    expect(onSelect).toHaveBeenCalledWith('10001')
  })

  it('renders empty state', () => {
    render(<SessionList sessions={[]} activeSessionId={null} onSelect={() => undefined} />)

    expect(screen.getByText('暂无会话')).toBeTruthy()
  })
})
