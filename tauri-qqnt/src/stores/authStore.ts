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
}

export const useAuthStore = create<AuthState>((set) => ({
  isAuthenticated: false,
  currentUser: null,
  serverHost: '127.0.0.1',
  serverPort: 6379,
  login: (user) => set({ isAuthenticated: true, currentUser: user }),
  logout: () => set({ isAuthenticated: false, currentUser: null }),
  setServer: (host, port) => set({ serverHost: host, serverPort: port })
}))