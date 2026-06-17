import { describe, expect, it } from 'vitest'
import { LOGIN_MIN_SIZE, LOGIN_SIZE, MAIN_MIN_SIZE, MAIN_SIZE } from './App'

describe('window sizes', () => {
  it('keeps login/register compact and main shell resizable', () => {
    expect(LOGIN_SIZE).toEqual({ width: 300, height: 460 })
    expect(LOGIN_MIN_SIZE).toEqual(LOGIN_SIZE)
    expect(MAIN_SIZE.width).toBeGreaterThan(LOGIN_SIZE.width)
    expect(MAIN_SIZE.height).toBeGreaterThan(LOGIN_SIZE.height)
    expect(MAIN_MIN_SIZE.width).toBeLessThanOrEqual(MAIN_SIZE.width)
    expect(MAIN_MIN_SIZE.height).toBeLessThanOrEqual(MAIN_SIZE.height)
  })
})
