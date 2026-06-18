import { create } from 'zustand'
import type { User } from '@/types/qqnt'

interface AuthState {
  isAuthenticated: boolean
  currentUser: User | null
  serverHost: string
  serverPort: number
  login: (user: User) => void
  logout: () => void
  setServer: (host: string, port: number) => void
  setCurrentUser: (user: User | null) => void
}

export const DEFAULT_SERVER_HOST = '127.0.0.1'
export const DEFAULT_SERVER_PORT = 16000

export const useAuthStore = create<AuthState>((set) => ({
  isAuthenticated: false,
  currentUser: null,
  serverHost: DEFAULT_SERVER_HOST,
  serverPort: DEFAULT_SERVER_PORT,
  login: (user) => set({ isAuthenticated: true, currentUser: user }),
  logout: () => set({ isAuthenticated: false, currentUser: null }),
  setServer: (host, port) => set({ serverHost: host, serverPort: port }),
  setCurrentUser: (user) => set({ currentUser: user })
}))