import { describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import { MemoryRouter } from 'react-router-dom'
import { AppNav } from './AppNav'
import { APP_ENTRIES } from '@/config/appEntries'

describe('AppNav', () => {
  it('renders QQ NT entries without exposing preview labels', () => {
    const { container } = render(
      <MemoryRouter>
        <AppNav />
      </MemoryRouter>
    )

    for (const entry of APP_ENTRIES) {
      expect(screen.getByTitle(entry.label)).toBeTruthy()
    }
    expect(container.textContent).not.toContain('Mock')
  })

  it('highlights the current route after direct navigation', () => {
    render(
      <MemoryRouter initialEntries={['/settings']}>
        <AppNav />
      </MemoryRouter>
    )

    expect(screen.getByTitle('设置').getAttribute('aria-current')).toBe('page')
    expect(screen.getByTitle('消息').getAttribute('aria-current')).toBeNull()
  })
})
