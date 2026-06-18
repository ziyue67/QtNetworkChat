import { act, renderHook, waitFor } from '@testing-library/react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { connectServer, login as apiLogin, register as apiRegister, sendCommand } from '@/api/qqnt'
import { DEFAULT_SERVER_PORT, useAuthStore } from '@/stores/authStore'
import { ENGINE_UNAVAILABLE_MESSAGE, useEngine } from './useEngine'

type EventCallback = (event: { payload: unknown }) => void

const eventHandlers = new Map<string, EventCallback>()
const listenMock = vi.fn((event: string, handler: EventCallback) => {
  eventHandlers.set(event, handler)
  return Promise.resolve(() => eventHandlers.delete(event))
})

vi.mock('@tauri-apps/api/event', () => ({
  listen: (event: string, handler: EventCallback) => listenMock(event, handler)
}))

vi.mock('@/api/qqnt', () => ({
  sendCommand: vi.fn(),
  connectServer: vi.fn(),
  login: vi.fn(),
  register: vi.fn()
}))

describe('useEngine', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    eventHandlers.clear()
    vi.mocked(sendCommand).mockResolvedValue({
      type: 'ack',
      op: 'ready',
      reqId: 'ready',
      status: 'ok'
    })
    vi.mocked(connectServer).mockResolvedValue({
      type: 'ack',
      op: 'connect',
      reqId: 'connect',
      status: 'ok',
      payload: { connected: true, host: '127.0.0.1', port: DEFAULT_SERVER_PORT }
    })
    vi.mocked(apiLogin).mockResolvedValue({
      type: 'ack',
      op: 'login',
      reqId: 'login',
      status: 'ok',
      payload: { accepted: true, requiresConnect: true, mode: 'login' }
    })
    vi.mocked(apiRegister).mockResolvedValue({
      type: 'ack',
      op: 'register',
      reqId: 'register',
      status: 'ok',
      payload: { accepted: true, requiresConnect: true, mode: 'register' }
    })
    useAuthStore.setState({
      isAuthenticated: false,
      currentUser: null,
      serverHost: '127.0.0.1',
      serverPort: DEFAULT_SERVER_PORT,
      mock: false
    })
  })

  it('does not enter mock mode when the local engine is unavailable', async () => {
    vi.mocked(sendCommand).mockRejectedValueOnce(new Error('invoke unavailable'))

    const { result } = renderHook(() => useEngine())

    await waitFor(() => {
      expect(result.current.engine.error).toBe(ENGINE_UNAVAILABLE_MESSAGE)
    })

    expect(result.current.engine.ready).toBe(false)
    expect(result.current.engine.mock).toBe(false)
    expect(useAuthStore.getState().mock).toBe(false)
  })

  it('does not authenticate locally when connect fails', async () => {
    vi.mocked(sendCommand).mockRejectedValueOnce(new Error('invoke unavailable'))
    vi.mocked(connectServer).mockRejectedValueOnce(new Error('invoke unavailable'))

    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const connected = await result.current.connect()
      expect(connected).toBe(false)
      await result.current.login('10001', 'secret')
    })

    expect(apiLogin).toHaveBeenCalledWith('10001', 'secret')
    expect(useAuthStore.getState().isAuthenticated).toBe(false)
    expect(useAuthStore.getState().mock).toBe(false)
  })

  it('returns the login handshake ack without authenticating before login_result', async () => {
    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const auth = await result.current.login('10001', 'secret')
      expect(auth).toEqual({ ok: true, requiresConnect: true })
    })

    expect(apiLogin).toHaveBeenCalledWith('10001', 'secret')
    expect(useAuthStore.getState().isAuthenticated).toBe(false)
    expect(result.current.engine.loggingIn).toBe(true)
  })

  it('returns the register handshake ack before the connect step', async () => {
    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const auth = await result.current.register('10002', 'secret', 'Alice')
      expect(auth).toEqual({ ok: true, requiresConnect: true })
    })

    expect(apiRegister).toHaveBeenCalledWith('10002', 'secret', 'Alice')
    expect(useAuthStore.getState().isAuthenticated).toBe(false)
    expect(result.current.engine.loggingIn).toBe(false)
  })

  it('maps auth-before-connect failures to the Chinese login error', async () => {
    vi.mocked(connectServer).mockRejectedValueOnce(
      new Error('Send login or register credentials before connect.')
    )

    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const connected = await result.current.connect()
      expect(connected).toBe(false)
    })

    await waitFor(() => {
      expect(result.current.engine.error).toBe('请先提交账号密码，再连接本地聊天服务。')
    })
  })

  it('uses the QQNT server sidecar default port when connecting', async () => {
    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const connected = await result.current.connect()
      expect(connected).toBe(true)
    })

    expect(connectServer).toHaveBeenCalledWith('127.0.0.1', 8888)
  })

  it('maps server connection ack failures to a Chinese diagnostic', async () => {
    vi.mocked(connectServer).mockResolvedValueOnce({
      type: 'ack',
      op: 'connect',
      reqId: 'connect',
      status: 'error',
      error: {
        code: 'connect_failed',
        message: 'Unable to connect to server.'
      }
    })

    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const connected = await result.current.connect()
      expect(connected).toBe(false)
    })

    await waitFor(() => {
      expect(result.current.engine.error).toBe('无法连接到本地聊天服务器，请确认 QQNTServer 已启动并监听 8888 端口。')
    })
  })

  it('maps server connection command failures to a Chinese diagnostic', async () => {
    vi.mocked(connectServer).mockRejectedValueOnce(new Error('Unable to connect to server.'))

    const { result } = renderHook(() => useEngine())

    await act(async () => {
      const connected = await result.current.connect()
      expect(connected).toBe(false)
    })

    await waitFor(() => {
      expect(result.current.engine.error).toBe('无法连接到本地聊天服务器，请确认 QQNTServer 已启动并监听 8888 端口。')
    })
  })

  it('authenticates only after the engine emits login_result', async () => {
    const { result } = renderHook(() => useEngine())

    await waitFor(() => {
      expect(eventHandlers.has('qqnt://engine/login_result')).toBe(true)
    })

    await act(async () => {
      await result.current.login('10003', 'secret')
      eventHandlers.get('qqnt://engine/login_result')?.({
        payload: {
          success: true,
          userId: '10003',
          userName: 'Alice'
        }
      })
    })

    expect(useAuthStore.getState().isAuthenticated).toBe(true)
    expect(useAuthStore.getState().currentUser?.id).toBe('10003')
  })
})
