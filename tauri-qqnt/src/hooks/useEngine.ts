import { useCallback, useEffect, useRef, useState } from 'react'
import { listen } from '@tauri-apps/api/event'
import {
  sendCommand,
  connectServer,
  login as apiLogin,
  register as apiRegister
} from '@/api/qqnt'
import { useAuthStore } from '@/stores/authStore'
import { useContactStore } from '@/stores/contactStore'
import { useFileStore } from '@/stores/fileStore'
import { useMessageStore } from '@/stores/messageStore'
import { useSessionStore } from '@/stores/sessionStore'
import type {
  ConnectionStatePayload,
  Contact,
  EngineReadyPayload,
  EngineState,
  FileDonePayload,
  FileErrorPayload,
  FileProgressPayload,
  FriendEventPayload,
  FriendSearchResultPayload,
  GroupMemberUpdatedPayload,
  GroupSnapshotPayload,
  LoginResultPayload,
  Message,
  MessageEventPayload,
  QQNTEventType,
  RawMessage,
  Session,
  UserListPayload,
  UserPresencePayload
} from '@/types/qqnt'
import { EXPECTED_PROTOCOL_VERSION } from '@/types/qqnt'

export interface UseEngineReturn {
  engine: EngineState
  connect: (host?: string, port?: number) => Promise<boolean>
  login: (account: string, password: string) => Promise<void>
  register: (account: string, password: string, userName: string) => Promise<boolean>
  logout: () => void
}

function asContact(userId: string, userName: string, online = false): Contact {
  return {
    id: userId,
    nickname: userName,
    status: online ? 'online' : 'offline'
  }
}

function rawToMessage(raw: RawMessage): Message {
  const fileInfo = raw.contentType === 'file' || raw.contentType === 'image'
    ? {
        id: raw.clientMessageId || raw.messageId,
        name: raw.content,
        size: 0,
        mime: raw.contentType === 'image' ? 'image/*' : 'application/octet-stream',
        progress: raw.status === 'sent' || raw.status === 'received' || raw.status === 'read' ? 100 : 0
      }
    : undefined

  return {
    id: raw.clientMessageId || raw.messageId,
    messageId: raw.messageId,
    clientMessageId: raw.clientMessageId,
    sessionId: raw.sessionId,
    senderId: raw.senderId,
    senderName: raw.senderName,
    type: raw.contentType,
    content: raw.content,
    timestamp: raw.timestamp,
    status: raw.status,
    fileInfo
  }
}

function sessionPreview(message: Message) {
  if (message.type === 'image') return '[图片]'
  if (message.type === 'file') return `[文件] ${message.fileInfo?.name || message.content}`
  return message.content
}

function upsertIncomingSession(message: Message, currentUserId?: string) {
  const sessionStore = useSessionStore.getState()
  const activeSessionId = sessionStore.activeSessionId
  const existing = sessionStore.sessions.find((session) => session.id === message.sessionId)
  const isMine = message.senderId === currentUserId
  const unread = activeSessionId === message.sessionId || isMine ? existing?.unread ?? 0 : (existing?.unread ?? 0) + 1
  const fallbackName = isMine ? existing?.name || message.sessionId : message.senderName
  const session: Session = {
    id: message.sessionId,
    type: message.sessionId.startsWith('g-') ? 'group' : 'private',
    name: existing?.name || fallbackName,
    avatar: existing?.avatar,
    unread,
    pinned: existing?.pinned ?? false,
    members: existing?.members,
    lastMessage: sessionPreview(message),
    lastTime: message.timestamp
  }
  sessionStore.upsertSession(session)
}

function handleMessageEvent(payload: MessageEventPayload) {
  const message = rawToMessage({ ...payload.message, sessionId: payload.sessionId || payload.message.sessionId })
  useMessageStore.getState().addMessage(message.sessionId, message)
  upsertIncomingSession(message, useAuthStore.getState().currentUser?.id)
}

function handleFileProgress(payload: FileProgressPayload) {
  const progress = payload.total > 0 ? (payload.bytes / payload.total) * 100 : 0
  useFileStore.getState().setProgress(payload.transferId, progress, {
    name: payload.fileName,
    size: payload.total,
    mime: 'application/octet-stream'
  })
}

function handleFileDone(payload: FileDonePayload) {
  useFileStore.getState().completeTransfer(payload.transferId, {
    name: payload.fileName,
    path: payload.filePath,
    mime: 'application/octet-stream'
  })
}

function handleFileError(payload: FileErrorPayload) {
  useFileStore.getState().failTransfer(payload.transferId, payload.reason)
}

function handleUserList(payload: UserListPayload) {
  useContactStore.getState().setContacts(
    payload.users.map((user) => asContact(user.userId, user.userName, Boolean(user.online)))
  )
}

function handlePresence(payload: UserPresencePayload, online: boolean) {
  const contactStore = useContactStore.getState()
  const contact = asContact(payload.userId, payload.userName, online)
  contactStore.addContact(contact)
  contactStore.updatePresence(payload.userId, online ? 'online' : 'offline')
}

function handleFriendEvent(payload: FriendEventPayload) {
  const contactStore = useContactStore.getState()
  if (payload.type !== 'accepted') return
  const id = payload.senderId || payload.receiverId
  const name = payload.senderName || id
  if (!id || !name) return
  contactStore.addContact(asContact(id, name, true))
}

function handleFriendSearchResult(payload: FriendSearchResultPayload) {
  if (!payload.found || !payload.userId || !payload.userName) return
  useContactStore.getState().addContact(asContact(payload.userId, payload.userName, Boolean(payload.online)))
}

function handleGroupSnapshot(payload: GroupSnapshotPayload) {
  const contactStore = useContactStore.getState()
  contactStore.setGroups(
    payload.groups.map((group) => ({
      id: group.groupId,
      nickname: group.groupName,
      status: 'online',
      signature: group.announcement || `${group.members?.length ?? 0} 名成员`,
      announcement: group.announcement,
      memberCount: group.members?.length ?? 0,
      members: group.members?.map((member) => ({
        id: member.userId,
        nickname: member.userName,
        status: 'online' as const,
        signature: member.role === 'owner' ? '群主' : member.role === 'admin' ? '管理员' : '成员'
      }))
    }))
  )
}

function handleGroupMemberUpdated(payload: GroupMemberUpdatedPayload) {
  const contactStore = useContactStore.getState()
  const group = contactStore.groups.find((item) => item.id === payload.groupId)
  if (!group) return
  const verb = payload.action === 'join' ? '加入' : payload.action === 'leave' ? '离开' : payload.action
  contactStore.addGroup({ ...group, signature: `${payload.userId} ${verb}群聊` })
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
  const setMockStore = useAuthStore((state) => state.setMock)
  const serverHost = useAuthStore((state) => state.serverHost)
  const serverPort = useAuthStore((state) => state.serverPort)

  const timersRef = useRef<number[]>([])
  const mockRef = useRef(false)

  const setMockIfNeeded = useCallback(
    (mock: boolean) => {
      mockRef.current = mock
      setMockStore(mock)
    },
    [setMockStore]
  )

  const clearTimers = useCallback(() => {
    timersRef.current.forEach((timer) => window.clearTimeout(timer))
    timersRef.current = []
  }, [])

  const setError = useCallback((message?: string) => {
    setEngine((prev) => ({ ...prev, error: message, loggingIn: false, connecting: false }))
  }, [])

  useEffect(() => {
    const unlisteners: (() => void)[] = []
    let cancelled = false

    async function bind() {
      const handlers: Array<[QQNTEventType | string, (payload: unknown) => void]> = [
        ['qqnt://engine/ready', (payload) => {
          const readyPayload = payload as EngineReadyPayload
          clearTimers()
          setMockIfNeeded(false)
          setEngine((prev) => ({
            ...prev,
            ready: true,
            connecting: false,
            protocolVersion: readyPayload.protocolVersion ?? EXPECTED_PROTOCOL_VERSION,
            mock: false,
            error: readyPayload.protocolVersion !== EXPECTED_PROTOCOL_VERSION
              ? `协议版本不匹配：engine=${readyPayload.protocolVersion}, expected=${EXPECTED_PROTOCOL_VERSION}`
              : prev.error
          }))
        }],
        ['qqnt://engine/connection_state', (payload) => {
          const connection = payload as ConnectionStatePayload
          setEngine((prev) => ({
            ...prev,
            connected: connection.connected,
            connecting: false
          }))
        }],
        ['qqnt://engine/login_result', (payload) => {
          const result = payload as LoginResultPayload
          setEngine((prev) => ({ ...prev, loggingIn: false }))
          if (result.success) {
            loginStore({
              id: result.userId ?? 'unknown',
              nickname: result.userName ?? 'QQ 用户',
              status: 'online'
            })
          } else {
            setError(result.error?.message || '登录失败')
          }
        }],
        ['qqnt://engine/user_list', (payload) => handleUserList(payload as UserListPayload)],
        ['qqnt://engine/user_joined', (payload) => handlePresence(payload as UserPresencePayload, true)],
        ['qqnt://engine/user_left', (payload) => handlePresence(payload as UserPresencePayload, false)],
        ['qqnt://engine/friend_event', (payload) => handleFriendEvent(payload as FriendEventPayload)],
        ['qqnt://engine/friend_search_result', (payload) => handleFriendSearchResult(payload as FriendSearchResultPayload)],
        ['qqnt://engine/message', (payload) => handleMessageEvent(payload as MessageEventPayload)],
        ['qqnt://engine/group_snapshot', (payload) => handleGroupSnapshot(payload as GroupSnapshotPayload)],
        ['qqnt://engine/group_member_updated', (payload) => handleGroupMemberUpdated(payload as GroupMemberUpdatedPayload)],
        ['qqnt://engine/file_progress', (payload) => handleFileProgress(payload as FileProgressPayload)],
        ['qqnt://engine/file_done', (payload) => handleFileDone(payload as FileDonePayload)],
        ['qqnt://engine/file_error', (payload) => handleFileError(payload as FileErrorPayload)],
        ['qqnt://engine/error', (payload) => {
          const error = payload as { message?: string }
          setError(error.message || '引擎错误')
        }],
        ['qqnt://server/fatal', (payload) => {
          const fatal = payload as { message?: string }
          setError(`服务端致命错误：${fatal.message || '未知错误'}`)
        }]
      ]

      for (const [event, handler] of handlers) {
        const unlisten = await listen(event, (evt) => {
          if (!cancelled) handler(evt.payload)
        })
        unlisteners.push(unlisten)
      }

      timersRef.current.push(
        window.setTimeout(() => {
          setEngine((prev) => {
            if (prev.ready) return prev
            setMockIfNeeded(true)
            return {
              ...prev,
              ready: true,
              connecting: false,
              mock: true,
              error: '未检测到本地 QQ NT 引擎，已临时进入 Mock 模式；可在设置页网络中调整地址后重连。'
            }
          })
        }, 4000)
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
      unlisteners.forEach((unlisten) => unlisten())
    }
  }, [clearTimers, loginStore, setError, setMockIfNeeded])

  const connect = useCallback(
    async (host?: string, port?: number) => {
      const connectHost = host ?? serverHost
      const connectPort = port ?? serverPort
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
        const ack = await connectServer(connectHost, connectPort)
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
            setMockIfNeeded(true)
            loginStore({ id: account, nickname: account || 'QQ 用户', status: 'online' })
          }, 600)
        )
        return
      }

      try {
        const ack = await apiLogin(account, password)
        if (ack.status === 'error') {
          setError(ack.error?.message || '登录失败')
        }
      } catch (err) {
        const message = err instanceof Error ? err.message : '登录请求失败'
        setError(message)
      }
    },
    [loginStore, setError, setMockIfNeeded]
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
