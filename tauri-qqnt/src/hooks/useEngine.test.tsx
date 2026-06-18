import { act, renderHook, waitFor } from '@testing-library/react'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { connectServer, login as apiLogin, sendCommand } from '@/api/qqnt'
import { useAuthStore } from '@/stores/authStore'
import { ENGINE_UNAVAILABLE_MESSAGE, useEngine } from './useEngine'

type EventCallback = (event: { payload: unknown }) => void

const listenMock = vi.fn((_event: string, _handler: EventCallback) => Promise.resolve(vi.fn()))

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
    useAuthStore.setState({
      isAuthenticated: false,
      currentUser: null,
      serverHost: '127.0.0.1',
      serverPort: 16000,
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
})
