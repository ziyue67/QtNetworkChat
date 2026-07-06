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

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(async (_command: string, args: { filePath: string }) => ({
    dataUrl: `data:image/png;base64,${args.filePath}`,
    base64: args.filePath
  }))
}))

vi.mock('react-window', async () => {
  const actual = await vi.importActual<typeof import('react-window')>('react-window')
  return {
    ...actual,
    VariableSizeList: ({
      children,
      itemCount,
      itemData,
      itemSize
    }: {
      children: (props: { index: number; style: React.CSSProperties; data: unknown }) => React.ReactNode
      itemCount: number
      itemData: unknown
      itemSize: (index: number) => number
    }) => (
      <div data-testid="virtual-list">
        {Array.from({ length: Math.min(itemCount, 40) }, (_, index) => (
          <div key={index} data-testid="virtual-row" data-size={itemSize(index)}>
            {children({ index, style: { height: itemSize(index) }, data: itemData })}
          </div>
        ))}
      </div>
    )
  }
})

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

  it('reserves enough virtual row height for image previews', async () => {
    const messages: Message[] = [
      {
        id: 'image-1',
        sessionId: 's-1',
        senderId: currentUser.id,
        senderName: currentUser.nickname,
        type: 'image',
        content: '5f79699f-4cd3-42b9-9340',
        timestamp: Date.now(),
        status: 'sent',
        fileInfo: {
          id: 'transfer-1',
          name: '5f79699f-4cd3-42b9-9340',
          size: 354611,
          mime: 'image/*',
          progress: 100,
          path: 'C:/tmp/5f79699f-4cd3-42b9-9340'
        }
      },
      createMessage(1)
    ]

    render(
      <div style={{ width: 720, height: 640 }}>
        <MessageList messages={messages} currentUser={currentUser} />
      </div>
    )

    expect(Number(screen.getAllByTestId('virtual-row')[0].getAttribute('data-size'))).toBeLessThan(320)
    expect(await screen.findByRole('img', { name: '图片预览' })).toBeTruthy()
  })
})
