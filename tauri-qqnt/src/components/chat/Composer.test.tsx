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
})
