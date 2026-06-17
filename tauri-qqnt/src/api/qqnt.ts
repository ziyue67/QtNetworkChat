import { invoke } from '@tauri-apps/api/core'
import type {
  QQNTCommand,
  QQNTAck,
  QQNTCommandOp,
  ConnectPayload,
  LoginPayload,
  RegisterPayload,
  SendPrivateMessagePayload,
  SendGroupMessagePayload,
  SendFilePayload,
  SendImagePayload,
  FriendSearchPayload,
  SendFriendRequestPayload,
  RespondFriendRequestPayload,
  CreateGroupPayload,
  UpdateGroupAnnouncementPayload,
  UpdateGroupMemberPayload,
  CancelTransferPayload,
  QueryResumePayload,
  ProfileUpdatePayload,
  SettingsSyncPayload,
  E2EStatusPayload,
  E2EAnnouncePayload,
  E2EPinPayload,
  E2ERotationPayload
} from '@/types/qqnt'

export type { QQNTCommand, QQNTAck, QQNTEvent } from '@/types/qqnt'

export function newReqId(): string {
  return crypto.randomUUID()
}

export async function sendCommand<Payload, Result>(
  op: QQNTCommandOp,
  payload?: Payload
): Promise<QQNTAck<Result>> {
  const command: QQNTCommand<Payload> = {
    op,
    reqId: newReqId(),
    payload
  }
  return invoke<QQNTAck<Result>>('qqnt_command', { payload: command })
}

// 兼容独立命令 wrapper（与 docs/qqnt-ipcv1.md 的统一入口并存）
export async function invokeCommand<Result>(command: string, args: Record<string, unknown>): Promise<Result> {
  return invoke<Result>(command, args)
}

export async function connectServer(host: string, port: number) {
  return sendCommand<ConnectPayload, ConnectionResult>('connect', { host, port })
}

export async function disconnectServer() {
  return sendCommand<void, void>('disconnect')
}

export async function login(account: string, password: string) {
  return sendCommand<LoginPayload, LoginResult>('login', { account, password })
}

export async function register(account: string, password: string, userName: string) {
  return sendCommand<RegisterPayload, RegisterResult>('register', { account, password, userName })
}

export async function logout() {
  return sendCommand<void, void>('logout')
}

export async function sendPrivateMessage(receiverId: string, content: string) {
  return sendCommand<SendPrivateMessagePayload, SendMessageResult>('send_private_message', {
    receiverId,
    content
  })
}

export async function sendGroupMessage(groupId: string, content: string) {
  return sendCommand<SendGroupMessagePayload, SendMessageResult>('send_group_message', {
    groupId,
    content
  })
}

export async function getUserList() {
  return sendCommand<void, UserListResult>('get_user_list')
}

export async function getFriendList() {
  return sendCommand<void, FriendListResult>('get_friend_list')
}

export async function getGroupList() {
  return sendCommand<void, GroupListResult>('get_group_list')
}

export async function searchFriend(account: string) {
  return sendCommand<FriendSearchPayload, FriendSearchResult>('search_friend', { account })
}

export async function sendFriendRequest(receiverId: string) {
  return sendCommand<SendFriendRequestPayload, void>('send_friend_request', { receiverId })
}

export async function respondFriendRequest(senderId: string, accepted: boolean) {
  return sendCommand<RespondFriendRequestPayload, void>('respond_friend_request', {
    senderId,
    accepted
  })
}

export async function createGroup(groupName: string, members: string[], announcement?: string) {
  return sendCommand<CreateGroupPayload, CreateGroupResult>('create_group', {
    groupName,
    members,
    announcement
  })
}

export async function updateGroupAnnouncement(groupId: string, announcement: string) {
  return sendCommand<UpdateGroupAnnouncementPayload, void>('update_group_announcement', {
    groupId,
    announcement
  })
}

export async function updateGroupMember(groupId: string, memberId: string, action: 'kick' | 'set_admin' | 'unset_admin') {
  return sendCommand<UpdateGroupMemberPayload, void>('update_group_member', {
    groupId,
    memberId,
    action
  })
}

export async function sendFile(args: SendFilePayload) {
  return sendCommand<SendFilePayload, FileTransferResult>('send_file', args)
}

export async function sendImage(args: SendImagePayload) {
  return sendCommand<SendImagePayload, FileTransferResult>('send_image', args)
}

export async function cancelTransfer(transferId: string) {
  return sendCommand<CancelTransferPayload, void>('cancel_transfer', { transferId })
}

export async function queryResume(filePath: string, transferId?: string, receiverId?: string) {
  return sendCommand<QueryResumePayload, ResumeResult>('query_resume', { filePath, transferId, receiverId })
}

export async function e2eStatus(peerId: string) {
  return sendCommand<E2EStatusPayload, E2EStatusResult>('e2e_status', { peerId })
}

export async function e2eAnnounceIdentity(peerId: string) {
  return sendCommand<E2EAnnouncePayload, void>('e2e_announce_identity', { peerId })
}

export async function e2ePinIdentity(peerId: string, fingerprint?: string) {
  return sendCommand<E2EPinPayload, void>('e2e_pin_identity', { peerId, fingerprint })
}

export async function e2eRequestRotation(peerId: string) {
  return sendCommand<E2ERotationPayload, void>('e2e_request_rotation', { peerId })
}

export async function profileUpdate(patch: ProfileUpdatePayload) {
  return sendCommand<ProfileUpdatePayload, void>('profile_update', patch)
}

export async function settingsSync(settings: SettingsSyncPayload) {
  return sendCommand<SettingsSyncPayload, void>('settings_sync', { settings })
}

// -- ack 结果类型（按需扩展）--
interface ConnectionResult {
  connected: boolean
  host: string
  port: number
}

interface LoginResult {
  success: boolean
  userId?: string
  userName?: string
}

interface RegisterResult {
  success: boolean
  userId?: string
  userName?: string
}

interface SendMessageResult {
  messageId: string
  clientMessageId: string
}

interface UserListResult {
  users: Array<{ userId: string; userName: string; online?: boolean }>
}

interface FriendListResult {
  friends: Array<{ userId: string; userName: string; online?: boolean }>
}

interface GroupListResult {
  groups: Array<{ groupId: string; groupName: string; announcement?: string; memberCount?: number; role?: string }>
}

interface FriendSearchResult {
  found: boolean
  userId?: string
  userName?: string
  online?: boolean
}

interface CreateGroupResult {
  groupId: string
}

interface FileTransferResult {
  transferId: string
}

interface ResumeResult {
  canResume: boolean
  bytes: number
  total: number
}

interface E2EStatusResult {
  enabled: boolean
  peerId?: string
  rotationRequired: boolean
}
