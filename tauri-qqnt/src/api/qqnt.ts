import { invoke } from '@tauri-apps/api/core'
import type { QQNTCommand } from '@/types/qqnt'

export async function sendCommand<T = unknown>(command: QQNTCommand): Promise<T> {
  return invoke<T>('qqnt_command', { payload: command })
}

export async function connectServer(host: string, port: number): Promise<{
  connected: boolean
  host: string
  port: number
}> {
  return invoke('connect_server', {
    reqId: crypto.randomUUID(),
    host,
    port
  })
}

export async function login(account: string, password: string): Promise<{
  accepted: boolean
  requiresConnect: boolean
  mode: string
}> {
  return invoke('login', {
    reqId: crypto.randomUUID(),
    account,
    password
  })
}
