import { describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import { MessageList } from './MessageList'
import type { Message, User } from '@/types/qqnt'

vi.mock('react-virtualized-auto-sizer', () => ({
  AutoSizer: ({
    renderProp
  }: {
    renderProp: (size: { height: number; width: number }) => React.ReactNode
  }) => renderProp({ height: 640, width: 720 })
}))

const currentUser: User = {
  id: 'u-self',
  nickname: '我',
  status: 'online'
}

function createMessage(index: number): Message {
  return {
    id: `m-${index}`,
    sessionId: 's-1',
    senderId: index % 2 === 0 ? currentUser.id : 'u-friend',
    senderName: index % 2 === 0 ? currentUser.nickname : '好友',
    type: 'text',
    content: `消息 ${index}`,
    timestamp: Date.now() + index,
    status: 'sent'
  }
}

describe('MessageList', () => {
  it('virtualizes large histories instead of rendering every message', () => {
    const messages = Array.from({ length: 10000 }, (_, index) => createMessage(index))
    const { container } = render(
      <div style={{ width: 720, height: 640 }}>
        <MessageList messages={messages} currentUser={currentUser} />
      </div>
    )

    const renderedMessages = screen.getAllByText(/^消息 /)
    expect(renderedMessages.length).toBeLessThan(80)
    expect(container.textContent).not.toContain('消息 5000')
  })
})
