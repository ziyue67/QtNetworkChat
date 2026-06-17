import { create } from 'zustand'
import type { User } from '@/types/qqnt'

interface AuthState {
  isAuthenticated: boolean
  currentUser: User | null
  serverHost: string
  serverPort: number
  mock: boolean
  login: (user: User) => void
  logout: () => void
  setServer: (host: string, port: number) => void
  setCurrentUser: (user: User | null) => void
  setMock: (mock: boolean) => void
}

export const DEFAULT_SERVER_HOST = '127.0.0.1'
export const DEFAULT_SERVER_PORT = 16000

export const useAuthStore = create<AuthState>((set) => ({
  isAuthenticated: false,
  currentUser: null,
  serverHost: DEFAULT_SERVER_HOST,
  serverPort: DEFAULT_SERVER_PORT,
  mock: false,
  login: (user) => set({ isAuthenticated: true, currentUser: user }),
  logout: () => set({ isAuthenticated: false, currentUser: null, mock: false }),
  setServer: (host, port) => set({ serverHost: host, serverPort: port }),
  setCurrentUser: (user) => set({ currentUser: user }),
  setMock: (mock) => set({ mock })
}))