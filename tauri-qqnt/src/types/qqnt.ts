export type QQNTEventType =
  | 'qqnt://engine/ready'
  | 'qqnt://engine/connected'
  | 'qqnt://engine/disconnected'
  | 'qqnt://engine/message'
  | 'qqnt://engine/file_progress'
  | 'qqnt://server/fatal'
  | 'qqnt://contacts/presence'

export interface QQNTEvent<T = unknown> {
  type: QQNTEventType | string
  payload: T
}

export interface QQNTCommand {
  reqId: string
  method: string
  params: Record<string, unknown>
}

export interface User {
  id: string
  nickname: string
  avatar?: string
  status: 'online' | 'offline' | 'busy' | 'away'
  signature?: string
}

export interface Contact extends User {
  remark?: string
}

export interface Session {
  id: string
  type: 'private' | 'group'
  name: string
  avatar?: string
  lastMessage?: string
  lastTime?: number
  unread: number
  pinned: boolean
  members?: Contact[]
}

export interface Message {
  id: string
  sessionId: string
  senderId: string
  senderName: string
  type: 'text' | 'image' | 'file' | 'system'
  content: string
  timestamp: number
  status: 'sending' | 'sent' | 'failed'
  fileInfo?: FileInfo
}

export interface FileInfo {
  id: string
  name: string
  size: number
  mime: string
  progress: number
  path?: string
}

export interface EngineState {
  ready: boolean
  connected: boolean
  protocolVersion: string
  error?: string
}

export type ThemeMode = 'light' | 'dark' | 'system'