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
  SendImagePayload
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

// 兼容后端独立命令 wrapper（与 docs/qqnt-ipcv1.md 的统一入口并存）
export async function invokeCommand<Result>(
  command: string,
  args: Record<string, unknown>
): Promise<Result> {
  return invoke<Result>(command, args)
}

export async function connectServer(host: string, port: number) {
  return sendCommand<ConnectPayload, ConnectionResult>('connect', { host, port })
}

export async function disconnectServer() {
  return sendCommand<DisconnectPayload, void>('disconnect')
}

export async function login(account: string, password: string) {
  return sendCommand<LoginPayload, LoginResult>('login', { account, password })
}

export async function register(account: string, password: string, userName: string) {
  return sendCommand<RegisterPayload, RegisterResult>('register', { account, password, userName })
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

export async function sendFile(args: SendFilePayload) {
  return sendCommand<SendFilePayload, FileTransferResult>('send_file', args)
}

export async function sendImage(args: SendImagePayload) {
  return sendCommand<SendImagePayload, FileTransferResult>('send_image', args)
}

// -- ack 结果类型（按需扩展）--
interface ConnectionResult {
  connected: boolean
  host: string
  port: number
}

interface DisconnectPayload {}

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

interface FileTransferResult {
  transferId: string
}