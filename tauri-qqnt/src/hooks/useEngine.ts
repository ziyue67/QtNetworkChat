import { useCallback, useEffect, useState } from 'react'
import { listen } from '@tauri-apps/api/event'
import { invoke } from '@tauri-apps/api/core'
import {
  sendCommand,
  connectServer,
  login as apiLogin,
  register as apiRegister,
  getLocalChatActions
} from '@/api/qqnt'
import { useAuthStore } from '@/stores/authStore'
import { useContactStore } from '@/stores/contactStore'
import { useFileStore } from '@/stores/fileStore'
import { useMessageStore } from '@/stores/messageStore'
import { isGroupSessionId, normalizeSessionId, useSessionStore } from '@/stores/sessionStore'
import { useUIStore } from '@/stores/uiStore'
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

export const ENGINE_UNAVAILABLE_MESSAGE = '未检测到本地 QQ NT 引擎，请从 Tauri 客户端启动并确认本地引擎运行。'
const AUTH_BEFORE_CONNECT_MESSAGE = '请先提交账号密码，再连接本地聊天服务。'
const AUTH_BEFORE_CONNECT_RAW_MESSAGE = 'Send login or register credentials before connect'
const SERVER_CONNECT_FAILED_MESSAGE = '无法连接到本地聊天服务，请确认 QQNTServer 已启动。'
const SERVER_CONNECT_FAILED_RAW_MESSAGE = 'Unable to connect to server'

interface SavedLocalFileResponse {
  filePath?: string
  fileName?: string
  file_path?: string
  file_name?: string
}

function savedFilePath(saved?: SavedLocalFileResponse) {
  return saved?.filePath || saved?.file_path || ''
}

function savedFileName(saved?: SavedLocalFileResponse) {
  return saved?.fileName || saved?.file_name || ''
}

export interface AuthHandshakeResult {
  ok: boolean
  requiresConnect: boolean
}

export interface UseEngineReturn {
  engine: EngineState
  connect: (host?: string, port?: number) => Promise<boolean>
  login: (account: string, password: string) => Promise<AuthHandshakeResult>
  register: (account: string, password: string, userName: string) => Promise<AuthHandshakeResult>
  logout: () => void
}

function userFacingError(message: string) {
  if (message.includes(AUTH_BEFORE_CONNECT_RAW_MESSAGE)) return AUTH_BEFORE_CONNECT_MESSAGE
  if (message.includes(SERVER_CONNECT_FAILED_RAW_MESSAGE)) return SERVER_CONNECT_FAILED_MESSAGE
  return message
}

function connectionFailureMessage(error: unknown) {
  const message = error instanceof Error ? error.message : typeof error === 'string' ? error : ''
  const friendlyMessage = userFacingError(message)
  return friendlyMessage === message ? ENGINE_UNAVAILABLE_MESSAGE : friendlyMessage
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
        id: raw.transferId || raw.clientMessageId || raw.messageId,
        name: raw.fileName || raw.content,
        size: raw.fileSize ?? 0,
        mime: raw.contentType === 'image' ? 'image/*' : 'application/octet-stream',
        progress: raw.transferProgress ?? (raw.status === 'sent' || raw.status === 'received' || raw.status === 'read' ? 100 : 0),
        path: raw.filePath
      }
    : undefined

  return {
    id: raw.clientMessageId || raw.messageId,
    messageId: raw.messageId,
    clientMessageId: raw.clientMessageId,
    sessionId: normalizeSessionId(raw.sessionId),
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
  const sessionId = normalizeSessionId(message.sessionId)
  const existing = sessionStore.sessions.find((session) => normalizeSessionId(session.id) === sessionId)
  const isMine = message.senderId === currentUserId
  const unread = activeSessionId === sessionId || isMine ? existing?.unread ?? 0 : (existing?.unread ?? 0) + 1
  const fallbackName = isMine ? existing?.name || sessionId : message.senderName
  const session: Session = {
    id: sessionId,
    type: isGroupSessionId(sessionId) ? 'group' : 'private',
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
  const raw = { ...payload.message, sessionId: normalizeSessionId(payload.sessionId || payload.message.sessionId) }
  const message = rawToMessage(raw)
  useMessageStore.getState().addMessage(message.sessionId, message)
  upsertIncomingSession(message, useAuthStore.getState().currentUser?.id)
  if ((message.type === 'file' || message.type === 'image') && raw.fileData && !message.fileInfo?.path) {
    void saveIncomingFileData(message, raw.fileData)
  }
}

async function saveIncomingFileData(message: Message, fileData: string) {
  const directoryPath = await useUIStore.getState().ensureDefaultDownloadPath()
  const saved = await invoke<SavedLocalFileResponse>('save_base64_file_to_directory', {
    base64: fileData,
    directoryPath,
    fileName: message.fileInfo?.name || message.content || 'file.bin'
  })
  const filePath = savedFilePath(saved)
  const fileName = savedFileName(saved)
  if (!filePath) return
  useMessageStore.getState().updateFileMessage(message.fileInfo?.id || message.id, {
    path: filePath,
    name: fileName || message.fileInfo?.name || message.content || 'file.bin',
    progress: 100
  }, message.status)
}

function handleFileProgress(payload: FileProgressPayload) {
  const progress = payload.total > 0 ? (payload.bytes / payload.total) * 100 : 0
  useFileStore.getState().setProgress(payload.transferId, progress, {
    name: payload.fileName,
    size: payload.total,
    mime: 'application/octet-stream'
  })
  useMessageStore.getState().updateFileMessage(payload.transferId, {
    name: payload.fileName,
    size: payload.total,
    progress,
    mime: 'application/octet-stream'
  }, payload.direction === 'upload' || payload.direction === 'outgoing' ? 'sending' : undefined)
}

function handleFileDone(payload: FileDonePayload) {
  useFileStore.getState().completeTransfer(payload.transferId, {
    name: payload.fileName,
    path: payload.filePath,
    mime: 'application/octet-stream'
  })
  useMessageStore.getState().updateFileMessage(payload.transferId, {
    name: payload.fileName,
    path: payload.filePath,
    progress: 100,
    mime: 'application/octet-stream'
  }, payload.direction === 'upload' || payload.direction === 'outgoing' ? 'sent' : 'received')
}

function handleFileError(payload: FileErrorPayload) {
  useFileStore.getState().failTransfer(payload.transferId, payload.reason)
  useMessageStore.getState().updateFileMessage(payload.transferId, {
    name: payload.fileName,
    size: payload.total,
    error: payload.reason
  }, 'failed')
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

  const setError = useCallback((message?: string) => {
    setEngine((prev) => ({ ...prev, error: message, loggingIn: false, connecting: false }))
  }, [])

  useEffect(() => {
    const unlisteners: (() => void)[] = []
    let cancelled = false

    async function bind() {
      getLocalChatActions()
        .then((actions) => {
          if (!cancelled) useMessageStore.getState().applyLocalChatActions(actions)
        })
        .catch(() => undefined)

      const handlers: Array<[QQNTEventType | string, (payload: unknown) => void]> = [
        ['qqnt://engine/ready', (payload) => {
          const readyPayload = payload as EngineReadyPayload
          setMockStore(false)
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
            setError(userFacingError(result.error?.message || '登录失败'))
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
          setError(userFacingError(error.message || '引擎错误'))
        }],
        ['qqnt://server/fatal', (payload) => {
          const fatal = payload as { message?: string }
          setError(`本地引擎错误：${userFacingError(fatal.message || '未知错误')}`)
        }]
      ]

      for (const [event, handler] of handlers) {
        const unlisten = await listen(event, (evt) => {
          if (!cancelled) handler(evt.payload)
        })
        if (cancelled) {
          unlisten()
        } else {
          unlisteners.push(unlisten)
        }
      }

      try {
        await sendCommand('ready', {})
      } catch {
        if (!cancelled) {
          setMockStore(false)
          setEngine((prev) => ({ ...prev, ready: false, connecting: false, mock: false, error: ENGINE_UNAVAILABLE_MESSAGE }))
        }
      }
    }

    bind()
    return () => {
      cancelled = true
      unlisteners.forEach((unlisten) => unlisten())
    }
  }, [loginStore, setError, setMockStore])

  const connect = useCallback(
    async (host?: string, port?: number) => {
      const connectHost = host ?? serverHost
      const connectPort = port ?? serverPort
      setEngine((prev) => ({ ...prev, connecting: true, error: undefined }))

      try {
        const ack = await connectServer(connectHost, connectPort)
        if (ack.status === 'error') {
          setError(userFacingError(ack.error?.message || '连接失败'))
          return false
        }
        setEngine((prev) => ({ ...prev, connected: ack.payload?.connected ?? true, connecting: false }))
        return true
      } catch (err) {
        setError(connectionFailureMessage(err))
        return false
      }
    },
    [serverHost, serverPort, setError]
  )

  const login = useCallback(
    async (account: string, password: string): Promise<AuthHandshakeResult> => {
      setEngine((prev) => ({ ...prev, loggingIn: true, error: undefined }))

      try {
        const ack = await apiLogin(account, password)
        if (ack.status === 'error') {
          setError(userFacingError(ack.error?.message || '登录失败'))
          return { ok: false, requiresConnect: false }
        }
        const result = {
          ok: ack.payload?.accepted ?? true,
          requiresConnect: ack.payload?.requiresConnect ?? true
        }
        if (!result.ok || !result.requiresConnect) {
          setEngine((prev) => ({ ...prev, loggingIn: false }))
        }
        return result
      } catch (err) {
        const message = err instanceof Error ? err.message : '登录请求失败'
        setError(userFacingError(message))
        return { ok: false, requiresConnect: false }
      }
    },
    [setError]
  )

  const register = useCallback(
    async (account: string, password: string, userName: string): Promise<AuthHandshakeResult> => {
      setEngine((prev) => ({ ...prev, loggingIn: true, error: undefined }))

      try {
        const ack = await apiRegister(account, password, userName)
        if (ack.status === 'error') {
          setError(userFacingError(ack.error?.message || '注册失败'))
          return { ok: false, requiresConnect: false }
        }
        const result = {
          ok: ack.payload?.accepted ?? true,
          requiresConnect: ack.payload?.requiresConnect ?? true
        }
        setEngine((prev) => ({ ...prev, loggingIn: false }))
        return result
      } catch (err) {
        const message = err instanceof Error ? err.message : '注册请求失败'
        setError(userFacingError(message))
        return { ok: false, requiresConnect: false }
      }
    },
    [setError]
  )

  const logout = useCallback(() => {
    logoutStore()
    setEngine((prev) => ({
      ...prev,
      connected: false,
      loggingIn: false,
      error: undefined
    }))
    sendCommand('logout').catch(() => undefined)
  }, [logoutStore])

  return {
    engine,
    connect,
    login,
    register,
    logout
  }
}

