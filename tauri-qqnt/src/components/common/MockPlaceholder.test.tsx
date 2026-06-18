import { describe, expect, it } from 'vitest'
import { render } from '@testing-library/react'
import { MockPlaceholder } from './MockPlaceholder'

describe('MockPlaceholder', () => {
  it('uses client-facing copy for unfinished QQ NT entries', () => {
    const { container } = render(<MockPlaceholder title="空间" />)

    expect(container.textContent).toContain('功能正在准备中')
    expect(container.textContent).not.toContain('Phase')
    expect(container.textContent).not.toContain('后端')
    expect(container.textContent).not.toContain('Mock')
  })
})
