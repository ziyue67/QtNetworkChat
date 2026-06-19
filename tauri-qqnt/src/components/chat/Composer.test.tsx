import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/react'
import { Composer } from '@/components/chat/Composer'

describe('Composer', () => {
  it('renders placeholder and sends text on button click', () => {
    const onSend = vi.fn()
    render(<Composer onSend={onSend} placeholder="Type here" />)

    const input = screen.getByPlaceholderText('Type here')
    fireEvent.change(input, { target: { value: 'hello' } })
    fireEvent.click(screen.getByText('发送'))

    expect(onSend).toHaveBeenCalledWith('hello')
  })

  it('does not send empty content', () => {
    const onSend = vi.fn()
    render(<Composer onSend={onSend} />)

    fireEvent.click(screen.getByText('发送'))
    expect(onSend).not.toHaveBeenCalled()
  })

  it('shows desktop file buttons and drop hints when file sending is enabled', () => {
    const onPickFiles = vi.fn()
    const onPickImages = vi.fn()
    render(<Composer onSend={vi.fn()} canSendFiles onPickFiles={onPickFiles} onPickImages={onPickImages} />)

    fireEvent.click(screen.getByRole('button', { name: /发送文件/ }))
    fireEvent.click(screen.getByRole('button', { name: /发送图片/ }))

    expect(screen.getByText('拖入文件发送')).toBeTruthy()
    expect(onPickFiles).toHaveBeenCalledTimes(1)
    expect(onPickImages).toHaveBeenCalledTimes(1)
  })

  it('shows member suggestions after typing @ and inserts a mention', () => {
    render(<Composer onSend={vi.fn()} mentionCandidates={[{ id: 'u-1', nickname: '阿明' }]} />)

    const input = screen.getByPlaceholderText('输入消息...')
    fireEvent.change(input, { target: { value: '@' } })
    fireEvent.click(screen.getByRole('button', { name: /阿明/ }))

    expect(input).toHaveProperty('value', '@阿明 ')
  })

  it('inserts pending mentions from avatar context menus', () => {
    const onMentionConsumed = vi.fn()
    render(
      <Composer
        onSend={vi.fn()}
        pendingMention={{ id: 'u-2', nickname: '好友' }}
        onMentionConsumed={onMentionConsumed}
      />
    )

    expect(screen.getByPlaceholderText('输入消息...')).toHaveProperty('value', '@好友 ')
    expect(onMentionConsumed).toHaveBeenCalledTimes(1)
  })

  it('disables text input when sending is unavailable', () => {
    render(<Composer onSend={vi.fn()} disabled />)

    expect(screen.getByPlaceholderText('输入消息...')).toHaveProperty('disabled', true)
  })
})
