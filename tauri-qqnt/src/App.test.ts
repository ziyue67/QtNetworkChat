import { describe, expect, it, vi } from 'vitest'
import { profileUpdate } from '@/api/qqnt'
import { LOGIN_MIN_SIZE, LOGIN_SIZE, MAIN_MIN_SIZE, MAIN_SIZE, submitLogin, submitRegister } from './App'
import type { UseEngineReturn } from '@/hooks/useEngine'

vi.mock('@/api/qqnt', () => ({
  profileUpdate: vi.fn(async () => ({ status: 'ok' }))
}))

function fakeEngine(overrides: Partial<UseEngineReturn> = {}): UseEngineReturn {
  return {
    engine: {
      ready: false,
      connected: false,
      connecting: false,
      loggingIn: false,
      protocolVersion: 1,
      mock: false
    },
    connect: vi.fn(async () => true),
    login: vi.fn(async () => ({ ok: true, requiresConnect: true })),
    register: vi.fn(async () => ({ ok: true, requiresConnect: true })),
    logout: vi.fn(),
    ...overrides
  }
}

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

describe('auth submit flow', () => {
  it('sends login credentials before connecting to the engine', async () => {
    const calls: string[] = []
    const engine = fakeEngine({
      login: vi.fn(async () => {
        calls.push('login')
        return { ok: true, requiresConnect: true }
      }),
      connect: vi.fn(async () => {
        calls.push('connect')
        return true
      })
    })

    await submitLogin(engine, '123', 'secret')

    expect(engine.login).toHaveBeenCalledWith('123', 'secret')
    expect(engine.connect).toHaveBeenCalledTimes(1)
    expect(calls).toEqual(['login', 'connect'])
  })

  it('does not connect when login credentials are rejected', async () => {
    const engine = fakeEngine({
      login: vi.fn(async () => ({ ok: false, requiresConnect: true }))
    })

    await submitLogin(engine, '123', 'bad-secret')

    expect(engine.login).toHaveBeenCalledWith('123', 'bad-secret')
    expect(engine.connect).not.toHaveBeenCalled()
  })

  it('does not connect when the login ack does not require it', async () => {
    const engine = fakeEngine({
      login: vi.fn(async () => ({ ok: true, requiresConnect: false }))
    })

    await submitLogin(engine, '123', 'secret')

    expect(engine.connect).not.toHaveBeenCalled()
  })

  it('sends register credentials before connecting and returns the connect result', async () => {
    const calls: string[] = []
    const engine = fakeEngine({
      register: vi.fn(async () => {
        calls.push('register')
        return { ok: true, requiresConnect: true }
      }),
      connect: vi.fn(async () => {
        calls.push('connect')
        return false
      })
    })

    const ok = await submitRegister(engine, '456', 'secret', '小企鹅')

    expect(engine.register).toHaveBeenCalledWith('456', 'secret', '小企鹅')
    expect(profileUpdate).not.toHaveBeenCalled()
    expect(ok).toBe(false)
    expect(calls).toEqual(['register', 'connect'])
  })

  it('updates nickname and avatar after register connection succeeds', async () => {
    const engine = fakeEngine()

    const ok = await submitRegister(engine, '456', 'secret', '小企鹅', 'aGVsbG8=')

    expect(ok).toBe(true)
    expect(engine.register).toHaveBeenCalledWith('456', 'secret', '小企鹅')
    expect(profileUpdate).toHaveBeenCalledWith({ userName: '小企鹅', avatarBase64: 'aGVsbG8=' })
  })
})
