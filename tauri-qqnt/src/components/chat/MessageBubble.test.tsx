import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen } from '@testing-library/react'

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(async (_command: string, args: { filePath: string }) => ({
    dataUrl: `data:image/png;base64,${args.filePath}`,
    base64: args.filePath
  }))
}))

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

  it('mentions another user from avatar context menu', () => {
    const onMentionSender = vi.fn()
    render(
      <MessageBubble
        message={{ ...baseMessage, senderId: 'u-friend', senderName: '好友' }}
        isSelf={false}
        senderAvatar="avatar-url"
        onMentionSender={onMentionSender}
      />
    )

    fireEvent.contextMenu(screen.getByTitle('右键打开 好友 菜单'))
    fireEvent.click(screen.getByRole('menuitem', { name: /@好友/ }))
    expect(onMentionSender).toHaveBeenCalledWith({ id: 'u-friend', nickname: '好友', avatar: 'avatar-url' })
  })

  it('renders image messages as previews', async () => {
    render(
      <MessageBubble
        message={{
          ...baseMessage,
          type: 'image',
          content: 'photo.png',
          fileInfo: {
            id: 'transfer-1',
            name: 'photo.png',
            size: 4096,
            mime: 'image/*',
            progress: 100,
            path: 'C:/tmp/photo.png'
          }
        }}
        isSelf
      />
    )

    const image = await screen.findByRole('img', { name: '图片预览' })
    expect(image.getAttribute('src')).toBe('data:image/png;base64,C:/tmp/photo.png')
    expect(image.className).toContain('max-w-[180px]')
    expect(image.className).toContain('max-h-[220px]')
    expect(screen.queryByText('photo.png')).toBeNull()
  })

  it('opens the image preview on double click', async () => {
    const onPreviewImage = vi.fn()
    const message: Message = {
      ...baseMessage,
      type: 'image',
      content: 'photo.png',
      fileInfo: {
        id: 'transfer-1',
        name: 'photo.png',
        size: 4096,
        mime: 'image/*',
        progress: 100,
        path: 'C:/tmp/photo.png'
      }
    }
    render(<MessageBubble message={message} isSelf onPreviewImage={onPreviewImage} />)

    const image = await screen.findByRole('img', { name: '图片预览' })
    fireEvent.doubleClick(image)

    expect(onPreviewImage).toHaveBeenCalledWith(message)
  })

  it('renders extensionless image messages as previews', async () => {
    render(
      <MessageBubble
        message={{
          ...baseMessage,
          type: 'image',
          content: '5f79699f-4cd3-42b9-9340',
          fileInfo: {
            id: 'transfer-1',
            name: '5f79699f-4cd3-42b9-9340',
            size: 354611,
            mime: 'application/octet-stream',
            progress: 100,
            path: 'C:/tmp/5f79699f-4cd3-42b9-9340'
          }
        }}
        isSelf
      />
    )

    const image = await screen.findByRole('img', { name: '图片预览' })
    expect(image.getAttribute('src')).toBe('data:image/png;base64,C:/tmp/5f79699f-4cd3-42b9-9340')
    expect(screen.queryByText('5f79699f-4cd3-42b9-9340')).toBeNull()
  })
})



