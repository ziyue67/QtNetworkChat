import { describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import { TitleBar } from './TitleBar'

describe('TitleBar', () => {
  it('marks the title bar as draggable and supports close-only mode', () => {
    const { container } = render(<TitleBar variant="close-only" />)
    const header = container.querySelector('header')

    expect(header?.getAttribute('data-tauri-drag-region')).toBe('true')
    expect(screen.queryByLabelText('最小化')).toBeNull()
    expect(screen.queryByLabelText('最大化')).toBeNull()
    expect(screen.getByLabelText('关闭')).toBeTruthy()
  })
})
