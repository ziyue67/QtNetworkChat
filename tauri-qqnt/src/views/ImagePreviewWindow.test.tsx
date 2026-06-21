import { describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import { MemoryRouter, Route, Routes } from 'react-router-dom'

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(async (_command: string, args: { filePath: string }) => ({
    dataUrl: `data:image/png;base64,${args.filePath}`,
    base64: args.filePath
  }))
}))

import { ImagePreviewWindow } from './ImagePreviewWindow'

describe('ImagePreviewWindow', () => {
  it('loads the preview image from the routed file path', async () => {
    render(
      <MemoryRouter initialEntries={['/image-preview?path=C%3A%2Ftmp%2Fphoto.png']}>
        <Routes>
          <Route path="/image-preview" element={<ImagePreviewWindow />} />
        </Routes>
      </MemoryRouter>
    )

    const image = await screen.findByRole('img', { name: '图片预览' })
    expect(image.getAttribute('src')).toBe('data:image/png;base64,C:/tmp/photo.png')
    expect(screen.getByText('100%')).toBeTruthy()
  })
})
