import type { MouseEvent } from 'react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { startWindowDrag } from './window'

const startDragging = vi.fn(() => Promise.resolve())

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({ startDragging })
}))

function mouseDown(target: HTMLElement, button = 0) {
  return { button, target } as unknown as MouseEvent<HTMLElement>
}

describe('startWindowDrag', () => {
  beforeEach(() => {
    startDragging.mockClear()
  })

  it('starts dragging from non-interactive regions', () => {
    const region = document.createElement('div')

    startWindowDrag(mouseDown(region))

    expect(startDragging).toHaveBeenCalledTimes(1)
  })

  it('does not drag from form controls or right click', () => {
    const input = document.createElement('input')
    const region = document.createElement('div')

    startWindowDrag(mouseDown(input))
    startWindowDrag(mouseDown(region, 2))

    expect(startDragging).not.toHaveBeenCalled()
  })
})
