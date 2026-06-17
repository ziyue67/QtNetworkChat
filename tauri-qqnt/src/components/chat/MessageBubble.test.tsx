import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen } from '@testing-library/react'
import { MessageBubble } from './MessageBubble'
import type { Message } from '@/types/qqnt'

const baseMessage: Message = {
  id: 'client-1',
  sessionId: '10001',
  senderId: 'me',
  senderName: 'Me',
  type: 'text',
  content: 'hello',
  timestamp: 1710000000000,
  status: 'sent'
}

describe('MessageBubble', () => {
  it('renders text message content', () => {
    render(<MessageBubble message={baseMessage} isSelf />)

    expect(screen.getByText('hello')).toBeTruthy()
  })

  it('retries failed message when clicked', () => {
    const onRetry = vi.fn()
    render(<MessageBubble message={{ ...baseMessage, status: 'failed' }} isSelf onRetry={onRetry} />)

    fireEvent.click(screen.getByText('发送失败（点击重试）'))
    expect(onRetry).toHaveBeenCalledWith('client-1')
  })
})
