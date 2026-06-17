import { describe, it, expect } from 'vitest'
import { cn } from '@/lib/utils'

describe('cn', () => {
  it('merges tailwind classes', () => {
    expect(cn('px-2 py-1', 'px-4')).toBe('py-1 px-4')
  })

  it('handles conditional classes', () => {
    expect(cn('px-2', false && 'py-1', 'py-3')).toBe('px-2 py-3')
  })
})
