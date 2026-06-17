import { create } from 'zustand'
import type { ThemeMode } from '@/types/qqnt'

interface UIState {
  theme: ThemeMode
  sidebarCollapsed: boolean
  activeRoute: string
  setTheme: (theme: ThemeMode) => void
  setActiveRoute: (route: string) => void
  toggleSidebar: () => void
}

export const useUIStore = create<UIState>((set) => ({
  theme: 'system',
  sidebarCollapsed: false,
  activeRoute: '/messages',
  setTheme: (theme) => set({ theme }),
  setActiveRoute: (route) => set({ activeRoute: route }),
  toggleSidebar: () => set((state) => ({ sidebarCollapsed: !state.sidebarCollapsed }))
}))