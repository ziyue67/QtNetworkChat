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

  it('shows desktop file drop hints when file sending is enabled', () => {
    render(<Composer onSend={vi.fn()} canSendFiles />)

    expect(screen.getByText('拖入文件发送')).toBeTruthy()
    expect(screen.getByText('图片自动识别')).toBeTruthy()
  })

  it('disables text input when sending is unavailable', () => {
    render(<Composer onSend={vi.fn()} disabled />)

    expect(screen.getByPlaceholderText('输入消息...')).toHaveProperty('disabled', true)
  })
})
