import { useCallback, useEffect, useRef, useState } from 'react'
import { listen } from '@tauri-apps/api/event'
import {
  sendCommand,
  connectServer,
  login as apiLogin,
  register as apiRegister
} from '@/api/qqnt'
import { useAuthStore } from '@/stores/authStore'
import type {
  EngineState,
  EngineReadyPayload,
  ConnectionStatePayload,
  LoginResultPayload,
  User,
  QQNTEventType
} from '@/types/qqnt'
import { EXPECTED_PROTOCOL_VERSION } from '@/types/qqnt'

export interface UseEngineReturn {
  engine: EngineState
  connect: (host?: string, port?: number) => Promise<boolean>
  login: (account: string, password: string) => Promise<void>
  register: (account: string, password: string, userName: string) => Promise<boolean>
  logout: () => void
}

export function useEngine(): UseEngineReturn {
  const [engine, setEngine] = useState<EngineState>({
    ready: false,
    connected: false,
    connecting: false,
    loggingIn: false,
    protocolVersion: EXPECTED_PROTOCOL_VERSION,
    mock: false
  })

  const loginStore = useAuthStore((state) => state.login)
  const logoutStore = useAuthStore((state) => state.logout)
  const serverHost = useAuthStore((state) => state.serverHost)
  const serverPort = useAuthStore((state) => state.serverPort)

  const timersRef = useRef<number[]>([])
  const mockRef = useRef(false)

  const clearTimers = useCallback(() => {
    timersRef.current.forEach((t) => window.clearTimeout(t))
    timersRef.current = []
  }, [])

  const setError = useCallback((message?: string) => {
    setEngine((prev) => ({ ...prev, error: message, loggingIn: false, connecting: false }))
  }, [])

  // 监听引擎事件
  useEffect(() => {
    const unlisteners: (() => void)[] = []
    let cancelled = false

    async function bind() {
      const handlers: Array<[QQNTEventType | string, (payload: unknown) => void]> = [
        ['qqnt://engine/ready', (payload) => {
          const p = payload as EngineReadyPayload
          clearTimers()
          mockRef.current = false
          setEngine((prev) => ({
            ...prev,
            ready: true,
            connecting: false,
            protocolVersion: p.protocolVersion ?? EXPECTED_PROTOCOL_VERSION,
            mock: false,
            error: p.protocolVersion !== EXPECTED_PROTOCOL_VERSION
              ? `协议版本不匹配：engine=${p.protocolVersion}, expected=${EXPECTED_PROTOCOL_VERSION}`
              : prev.error
          }))
        }],
        ['qqnt://engine/connection_state', (payload) => {
          const p = payload as ConnectionStatePayload
          setEngine((prev) => ({
            ...prev,
            connected: p.connected,
            connecting: false
          }))
        }],
        ['qqnt://engine/login_result', (payload) => {
          const p = payload as LoginResultPayload
          setEngine((prev) => ({ ...prev, loggingIn: false }))
          if (p.success) {
            const user: User = {
              id: p.userId ?? 'unknown',
              nickname: p.userName ?? 'QQ 用户',
              status: 'online'
            }
            loginStore(user)
          } else {
            setError(p.error?.message || '登录失败')
          }
        }],
        ['qqnt://engine/error', (payload) => {
          const p = payload as { message?: string }
          setError(p.message || '引擎错误')
        }],
        ['qqnt://server/fatal', (payload) => {
          const p = payload as { message?: string }
          setError(`服务端致命错误：${p.message || '未知错误'}`)
        }]
      ]

      for (const [event, handler] of handlers) {
        const unlisten = await listen(event, (evt) => {
          if (!cancelled) handler(evt.payload)
        })
        unlisteners.push(unlisten)
      }

      // 尝试 ping 后端；无响应则进入 mock 模式
      timersRef.current.push(
        window.setTimeout(() => {
          setEngine((prev) => {
            if (prev.ready) return prev
            mockRef.current = true
            return { ...prev, ready: true, connecting: false, mock: true }
          })
        }, 1500)
      )

      try {
        await sendCommand('ready', {})
      } catch {
        // 后端未就绪，等 fallback 计时器进入 mock
      }
    }

    bind()
    return () => {
      cancelled = true
      clearTimers()
      unlisteners.forEach((u) => u())
    }
  }, [clearTimers, loginStore, setError])

  const connect = useCallback(
    async (host?: string, port?: number) => {
      const h = host ?? serverHost
      const p = port ?? serverPort
      setEngine((prev) => ({ ...prev, connecting: true, error: undefined }))

      if (mockRef.current) {
        return new Promise<boolean>((resolve) => {
          timersRef.current.push(
            window.setTimeout(() => {
              setEngine((prev) => ({ ...prev, connected: true, connecting: false }))
              resolve(true)
            }, 600)
          )
        })
      }

      try {
        const ack = await connectServer(h, p)
        if (ack.status === 'error') {
          setError(ack.error?.message || '连接服务器失败')
          return false
        }
        setEngine((prev) => ({ ...prev, connected: ack.payload?.connected ?? true, connecting: false }))
        return true
      } catch (err) {
        const message = err instanceof Error ? err.message : '连接失败，请检查服务端是否已启动'
        setError(message)
        return false
      }
    },
    [serverHost, serverPort, setError]
  )

  const login = useCallback(
    async (account: string, password: string) => {
      setEngine((prev) => ({ ...prev, loggingIn: true, error: undefined }))

      if (mockRef.current) {
        timersRef.current.push(
          window.setTimeout(() => {
            setEngine((prev) => ({ ...prev, loggingIn: false }))
            loginStore({ id: account, nickname: account || 'QQ 用户', status: 'online' })
          }, 600)
        )
        return
      }

      try {
        const ack = await apiLogin(account, password)
        if (ack.status === 'error') {
          setError(ack.error?.message || '登录失败')
          return
        }
        // 等待 login_result 事件更新登录态
      } catch (err) {
        const message = err instanceof Error ? err.message : '登录请求失败'
        setError(message)
      }
    },
    [loginStore, setError]
  )

  const register = useCallback(
    async (account: string, password: string, userName: string): Promise<boolean> => {
      setEngine((prev) => ({ ...prev, loggingIn: true, error: undefined }))

      if (mockRef.current) {
        return new Promise((resolve) => {
          timersRef.current.push(
            window.setTimeout(() => {
              setEngine((prev) => ({ ...prev, loggingIn: false }))
              resolve(true)
            }, 600)
          )
        })
      }

      try {
        const ack = await apiRegister(account, password, userName)
        setEngine((prev) => ({ ...prev, loggingIn: false }))
        if (ack.status === 'error') {
          setError(ack.error?.message || '注册失败')
          return false
        }
        return true
      } catch (err) {
        const message = err instanceof Error ? err.message : '注册请求失败'
        setError(message)
        return false
      }
    },
    [setError]
  )

  const logout = useCallback(() => {
    clearTimers()
    logoutStore()
    setEngine((prev) => ({
      ...prev,
      connected: false,
      loggingIn: false,
      error: undefined
    }))
    if (!mockRef.current) {
      sendCommand('logout').catch(() => undefined)
    }
  }, [clearTimers, logoutStore])

  return {
    engine,
    connect,
    login,
    register,
    logout
  }
}