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

  it('deduplicates backend echoes by clientMessageId', () => {
    useMessageStore.getState().addMessage('10001', { ...message, clientMessageId: 'client-1' })
    useMessageStore.getState().addMessage('10001', {
      ...message,
      id: 'server-1',
      messageId: 'server-1',
      clientMessageId: 'client-1',
      status: 'sent'
    })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({ id: 'server-1', status: 'sent' })
  })

  it('updates file message metadata by transfer id', () => {
    useMessageStore.getState().setMessages('10001', [
      {
        ...message,
        id: 'client-file-1',
        type: 'file',
        content: 'report.zip',
        fileInfo: {
          id: 'transfer-1',
          name: 'report.zip',
          size: 0,
          mime: 'application/octet-stream',
          progress: 0
        }
      }
    ])

    useMessageStore.getState().updateFileMessage('transfer-1', {
      size: 2048,
      progress: 50
    }, 'sending')

    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({
      status: 'sending',
      fileInfo: { id: 'transfer-1', size: 2048, progress: 50 }
    })
  })

  it('transitions optimistic file messages from client id to transfer id', () => {
    useMessageStore.getState().setMessages('10001', [
      {
        ...message,
        id: 'client-file-1',
        type: 'file',
        content: 'report.zip',
        fileInfo: {
          id: 'client-file-1',
          name: 'report.zip',
          size: 0,
          mime: 'application/octet-stream',
          progress: 0
        }
      }
    ])

    useMessageStore.getState().updateFileMessage('client-file-1', { id: 'transfer-1' }, 'sending')
    useMessageStore.getState().updateFileMessage('transfer-1', { progress: 75 }, 'sending')
    useMessageStore.getState().updateMessageStatus('10001', 'transfer-1', 'failed')

    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({
      status: 'failed',
      fileInfo: { id: 'transfer-1', progress: 75 }
    })
  })

  it('matches accepted file sends to later transfer events by file name', () => {
    useMessageStore.getState().setMessages('10001', [
      {
        ...message,
        id: 'client-file-1',
        type: 'file',
        content: 'report.zip',
        fileInfo: {
          id: 'client-file-1',
          name: 'report.zip',
          size: 0,
          mime: 'application/octet-stream',
          progress: 0
        }
      }
    ])

    useMessageStore.getState().updateFileMessage('transfer-1', {
      name: 'report.zip',
      progress: 100
    }, 'sent')

    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({
      status: 'sent',
      fileInfo: { id: 'transfer-1', name: 'report.zip', progress: 100 }
    })
  })
})
