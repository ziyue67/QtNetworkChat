import { invoke } from '@tauri-apps/api/core'
import type { QQNTCommand } from '@/types/qqnt'

export async function sendCommand<T = unknown>(command: QQNTCommand): Promise<T> {
  return invoke<T>('qqnt_command', { payload: command })
}

export async function connectServer(host: string, port: number): Promise<boolean> {
  return invoke('qqnt_command', {
    payload: {
      reqId: crypto.randomUUID(),
      method: 'connect',
      params: { host, port }
    }
  })
}

export async function login(account: string, password: string): Promise<{ userId: string }> {
  return invoke('qqnt_command', {
    payload: {
      reqId: crypto.randomUUID(),
      method: 'login',
      params: { account, password }
    }
  })
}