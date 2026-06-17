import { beforeEach, describe, expect, it } from 'vitest'
import { useMessageStore } from './messageStore'
import type { Message } from '@/types/qqnt'

const message: Message = {
  id: 'client-1',
  sessionId: '10001',
  senderId: 'me',
  senderName: 'Me',
  type: 'text',
  content: 'hello',
  timestamp: 1710000000000,
  status: 'sending'
}

describe('messageStore', () => {
  beforeEach(() => {
    useMessageStore.setState({ messages: {} })
  })

  it('appends messages by session', () => {
    useMessageStore.getState().addMessage('10001', message)

    expect(useMessageStore.getState().messages['10001']).toEqual([message])
  })

  it('updates a failed or sent status by id', () => {
    useMessageStore.getState().setMessages('10001', [message])
    useMessageStore.getState().updateMessageStatus('10001', 'client-1', 'failed')

    expect(useMessageStore.getState().messages['10001'][0].status).toBe('failed')
  })
})
