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
    useMessageStore.setState({ messages: {}, clearedSessionWatermarks: {}, deletedMessageIds: {} })
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


  it('deduplicates backend echoes without clientMessageId by recent outgoing content', () => {
    useMessageStore.getState().addMessage('10001', { ...message, sessionId: '10001' })
    useMessageStore.getState().addMessage('10001', {
      ...message,
      id: 'server-echo-1',
      messageId: 'server-echo-1',
      sessionId: '10001',
      timestamp: message.timestamp + 1500,
      status: 'sent'
    })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({ id: 'server-echo-1', status: 'sent' })
  })

  it('deduplicates backend echoes even when sender id differs', () => {
    useMessageStore.getState().addMessage('10001', { ...message, sessionId: '10001' })
    useMessageStore.getState().addMessage('10001', {
      ...message,
      id: 'server-echo-different-sender',
      messageId: 'server-echo-different-sender',
      sessionId: '10001',
      senderId: '10001',
      timestamp: message.timestamp + 800,
      status: 'sent'
    })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
  })

  it('deduplicates backend echoes when timestamps mix seconds and milliseconds', () => {
    useMessageStore.getState().addMessage('10001', { ...message, sessionId: '10001', timestamp: 1710000000000 })
    useMessageStore.getState().addMessage('10001', {
      ...message,
      id: 'server-echo-seconds',
      messageId: 'server-echo-seconds',
      sessionId: '10001',
      timestamp: 1710000001,
      status: 'sent'
    })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({ id: 'server-echo-seconds', status: 'sent' })
  })

  it('keeps repeated user sends as separate optimistic messages', () => {
    useMessageStore.getState().addMessage('10001', { ...message, id: 'client-1', sessionId: '10001' })
    useMessageStore.getState().addMessage('10001', { ...message, id: 'client-2', sessionId: '10001', timestamp: message.timestamp + 300 })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(2)
  })

  it('deduplicates duplicate server echoes by recent outgoing content', () => {
    useMessageStore.getState().addMessage('10001', {
      ...message,
      id: 'server-1',
      messageId: 'server-1',
      sessionId: '10001',
      status: 'sent'
    })
    useMessageStore.getState().addMessage('10001', {
      ...message,
      id: 'server-2',
      messageId: 'server-2',
      sessionId: '10001',
      timestamp: message.timestamp + 600,
      status: 'sent'
    })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
  })

  it('removes one message and clears a whole session', () => {
    useMessageStore.getState().setMessages('10001', [
      { ...message, id: 'client-1' },
      { ...message, id: 'client-2', content: 'second' }
    ])

    useMessageStore.getState().removeMessage('10001', 'client-1')
    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
    expect(useMessageStore.getState().messages['10001'][0].id).toBe('client-2')

    useMessageStore.getState().clearSessionMessages('10001')
    expect(useMessageStore.getState().messages['10001']).toEqual([])
  })

  it('restores recalled local actions as hidden messages', () => {
    useMessageStore.getState().setMessages('10001', [
      { ...message, id: 'server-1', messageId: 'server-1', status: 'sent' },
      { ...message, id: 'server-2', messageId: 'server-2', content: 'second', status: 'sent' }
    ])

    useMessageStore.getState().applyLocalChatActions({
      recalledMessages: [{ sessionId: '10001', messageId: 'server-1' }]
    })

    expect(useMessageStore.getState().messages['10001']).toHaveLength(1)
    expect(useMessageStore.getState().messages['10001'][0].id).toBe('server-2')
    expect(useMessageStore.getState().deletedMessageIds['10001']).toContain('server-1')
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

  it('preserves image mime when generic transfer events arrive', () => {
    useMessageStore.getState().setMessages('10001', [
      {
        ...message,
        id: 'client-image-1',
        type: 'image',
        content: 'pasted-image',
        fileInfo: {
          id: 'transfer-1',
          name: 'pasted-image',
          size: 0,
          mime: 'image/*',
          progress: 0,
          path: 'C:/tmp/pasted-image'
        }
      }
    ])

    useMessageStore.getState().updateFileMessage('transfer-1', {
      size: 4096,
      progress: 100,
      mime: 'application/octet-stream'
    }, 'sent')

    expect(useMessageStore.getState().messages['10001'][0]).toMatchObject({
      status: 'sent',
      fileInfo: { id: 'transfer-1', mime: 'image/*', progress: 100 }
    })
  })

  it('keeps the saved download path when later done events report the original source path', () => {
    useMessageStore.getState().setMessages('10001', [
      {
        ...message,
        id: 'client-file-1',
        type: 'file',
        content: 'report.zip',
        status: 'sent',
        fileInfo: {
          id: 'transfer-1',
          name: 'report.zip',
          size: 1024,
          mime: 'application/octet-stream',
          progress: 100,
          path: 'C:/Users/jun23/Downloads/test/report.zip'
        }
      }
    ])

    useMessageStore.getState().updateFileMessage('transfer-1', {
      path: 'D:/source/report.zip',
      progress: 100
    }, 'sent')

    expect(useMessageStore.getState().messages['10001'][0].fileInfo?.path).toBe('C:/Users/jun23/Downloads/test/report.zip')
  })
})
