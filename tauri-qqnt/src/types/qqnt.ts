// QQ NT IPC v1 类型定义
// 与 docs/qqnt-ipcv1.md 保持同步

export const EXPECTED_PROTOCOL_VERSION = 1

export type QQNTCommandOp =
  | 'ready'
  | 'connect'
  | 'disconnect'
  | 'login'
  | 'register'
  | 'logout'
  | 'set_user_info'
  | 'get_user_list'
  | 'get_friend_list'
  | 'get_group_list'
  | 'search_friend'
  | 'send_friend_request'
  | 'respond_friend_request'
  | 'send_private_message'
  | 'send_group_message'
  | 'create_group'
  | 'update_group_announcement'
  | 'update_group_member'
  | 'send_file'
  | 'send_image'
  | 'cancel_transfer'
  | 'query_resume'
  | 'e2e_status'
  | 'e2e_announce_identity'
  | 'e2e_pin_identity'
  | 'e2e_request_rotation'
  | 'profile_update'
  | 'settings_sync'

export type QQNTEventType =
  | 'qqnt://engine/ready'
  | 'qqnt://engine/connection_state'
  | 'qqnt://engine/login_result'
  | 'qqnt://engine/user_list'
  | 'qqnt://engine/user_joined'
  | 'qqnt://engine/user_left'
  | 'qqnt://engine/friend_event'
  | 'qqnt://engine/friend_search_result'
  | 'qqnt://engine/message'
  | 'qqnt://engine/group_snapshot'
  | 'qqnt://engine/group_member_updated'
  | 'qqnt://engine/file_progress'
  | 'qqnt://engine/file_done'
  | 'qqnt://engine/file_error'
  | 'qqnt://engine/e2e_session_state'
  | 'qqnt://engine/e2e_identity_state'
  | 'qqnt://engine/e2e_rotation_request'
  | 'qqnt://engine/e2e_rotation_response'
  | 'qqnt://engine/notification'
  | 'qqnt://engine/error'
  | 'qqnt://server/fatal'
  | `qqnt://engine/${string}`

export interface QQNTCommand<Payload = Record<string, unknown>> {
  op: QQNTCommandOp
  reqId: string
  payload?: Payload
}

export type QQNTAckStatus = 'ok' | 'error'

export interface QQNTError {
  code: string
  message: string
  source?: string
}

export interface QQNTAck<Payload = Record<string, unknown>> {
  type: 'ack'
  op: QQNTCommandOp
  reqId: string
  status: QQNTAckStatus
  payload?: Payload
  error?: QQNTError
}

export interface QQNTEvent<T = unknown> {
  type: QQNTEventType | string
  payload: T
}

// 命令 payload
export interface ReadyPayload {}

export interface ConnectPayload {
  host: string
  port: number
}

export interface DisconnectPayload {}

export interface LoginPayload {
  account: string
  password: string
}

export interface RegisterPayload {
  account: string
  password: string
  userName: string
}

export interface LogoutPayload {}

export interface SendPrivateMessagePayload {
  receiverId: string
  content: string
}

export interface SendGroupMessagePayload {
  groupId: string
  content: string
}

export interface SendFilePayload {
  filePath: string
  receiverId?: string
  groupId?: string
}

export interface SendImagePayload {
  filePath: string
  receiverId?: string
  groupId?: string
}

export interface FriendSearchPayload {
  account: string
}

export interface SendFriendRequestPayload {
  receiverId: string
}

export interface RespondFriendRequestPayload {
  senderId: string
  accepted: boolean
}

export interface CreateGroupPayload {
  groupName: string
  members: string[]
  announcement?: string
}

export interface UpdateGroupAnnouncementPayload {
  groupId: string
  announcement: string
}

export interface UpdateGroupMemberPayload {
  groupId: string
  memberId: string
  action: 'join' | 'leave' | 'kick' | 'set_admin' | 'unset_admin'
}

export interface CancelTransferPayload {
  transferId: string
}

export interface QueryResumePayload {
  filePath: string
  transferId?: string
  receiverId?: string
}

export interface ProfileUpdatePayload {
  userName?: string
  avatarBase64?: string
  signature?: string
}

export type SettingsSyncPayload = Record<string, unknown>

export interface E2EStatusPayload {
  peerId: string
}

export interface E2EAnnouncePayload {
  peerId: string
}

export interface E2EPinPayload {
  peerId: string
  fingerprint?: string
}

export interface E2ERotationPayload {
  peerId: string
}

// 事件 payload
export interface EngineReadyPayload {
  protocolVersion: number
  version: string
  qtVersion: string
  e2eStatus: string
}

export interface ConnectionStatePayload {
  connected: boolean
  host: string
  port: number
}

export interface LoginResultPayload {
  success: boolean
  userId?: string
  userName?: string
  error?: QQNTError
}

export interface UserListPayload {
  users: Array<{
    userId: string
    userName: string
    online?: boolean
  }>
}

export interface UserPresencePayload {
  userId: string
  userName: string
}

export interface FriendEventPayload {
  type: 'sent' | 'received' | 'accepted' | 'rejected'
  senderId?: string
  senderName?: string
  receiverId?: string
  accepted?: boolean
  delivered?: boolean
}

export interface FriendSearchResultPayload {
  found: boolean
  userId?: string
  userName?: string
  online?: boolean
}

export interface MessageEventPayload {
  sessionId: string
  message: RawMessage
}

export interface GroupSnapshotPayload {
  groups: Array<{
    groupId: string
    groupName: string
    announcement?: string
    members?: Array<{
      userId: string
      userName: string
      role?: 'owner' | 'admin' | 'member'
    }>
  }>
}

export interface GroupMemberUpdatedPayload {
  groupId: string
  userId: string
  action: 'join' | 'leave' | 'kick' | 'set_admin' | 'unset_admin'
}

export interface FileProgressPayload {
  transferId: string
  fileName: string
  bytes: number
  total: number
  direction: 'upload' | 'download'
}

export interface FileDonePayload {
  transferId: string
  fileName: string
  filePath: string
  direction: 'upload' | 'download'
}

export interface FileErrorPayload {
  transferId: string
  reason: string
}

export interface E2ESessionStatePayload {
  peerId: string
  rotationRequired: boolean
}

export interface E2EIdentityStatePayload {
  peerId: string
  trusted: boolean
  fingerprint?: string
}

export interface E2ERotationRequestPayload {
  peerId: string
  reason?: string
}

export interface E2ERotationResponsePayload {
  peerId: string
  accepted: boolean
}

export interface NotificationPayload {
  title: string
  body: string
}

export interface ServerFatalPayload {
  message: string
  error?: QQNTError
}

// UI 领域模型
export type ContentType = 'text' | 'image' | 'file' | 'system'
export type MessageStatus = 'sending' | 'sent' | 'received' | 'failed' | 'read'

export interface RawMessage {
  messageId: string
  clientMessageId: string
  sessionId: string
  senderId: string
  senderName: string
  contentType: ContentType
  content: string
  fileName?: string
  fileSize?: number
  filePath?: string
  transferId?: string
  transferProgress?: number
  timestamp: number
  status: MessageStatus
}

export interface User {
  id: string
  nickname: string
  avatar?: string
  status: 'online' | 'offline' | 'busy' | 'away'
  signature?: string
}

export interface QQNTAppEntry {
  id: string
  icon: string
  label: string
  path: string
  mock: boolean
}

export interface Contact extends User {
  remark?: string
  announcement?: string
  memberCount?: number
  members?: Contact[]
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
  messageId?: string
  clientMessageId?: string
  sessionId: string
  senderId: string
  senderName: string
  type: ContentType
  content: string
  timestamp: number
  status: MessageStatus
  fileInfo?: FileInfo
}

export interface FileInfo {
  id: string
  name: string
  size: number
  mime: string
  progress: number
  path?: string
  error?: string
}

export interface EngineState {
  ready: boolean
  connected: boolean
  connecting: boolean
  loggingIn: boolean
  protocolVersion: number
  mock: boolean
  error?: string
}

export type ThemeMode = 'light' | 'dark' | 'system'
