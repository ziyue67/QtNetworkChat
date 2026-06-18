import { create } from 'zustand'
import type { ThemeMode } from '@/types/qqnt'

export interface QQNTSettings {
  theme: ThemeMode
  launchOnStartup: boolean
  minimizeToTray: boolean
  notifications: boolean
  sound: boolean
  desktopNotifications: boolean
  muteInSession: boolean
  downloadPath: string
  autoAcceptFiles: boolean
  openFolderAfterDownload: boolean
  e2eEnabled: boolean
}

interface UIState {
  theme: ThemeMode
  sidebarCollapsed: boolean
  activeRoute: string
  settings: QQNTSettings
  setTheme: (theme: ThemeMode) => void
  setActiveRoute: (route: string) => void
  updateSettings: (patch: Partial<QQNTSettings>) => void
  toggleSidebar: () => void
}

const SETTINGS_KEY = 'qqnt:settings'

export const DEFAULT_SETTINGS: QQNTSettings = {
  theme: 'system',
  launchOnStartup: false,
  minimizeToTray: true,
  notifications: true,
  sound: true,
  desktopNotifications: true,
  muteInSession: false,
  downloadPath: '',
  autoAcceptFiles: false,
  openFolderAfterDownload: true,
  e2eEnabled: true
}

function sanitizeSettings(settings: Partial<QQNTSettings>): QQNTSettings {
  return {
    theme: settings.theme ?? DEFAULT_SETTINGS.theme,
    launchOnStartup: settings.launchOnStartup ?? DEFAULT_SETTINGS.launchOnStartup,
    minimizeToTray: settings.minimizeToTray ?? DEFAULT_SETTINGS.minimizeToTray,
    notifications: settings.notifications ?? DEFAULT_SETTINGS.notifications,
    sound: settings.sound ?? DEFAULT_SETTINGS.sound,
    desktopNotifications: settings.desktopNotifications ?? DEFAULT_SETTINGS.desktopNotifications,
    muteInSession: settings.muteInSession ?? DEFAULT_SETTINGS.muteInSession,
    downloadPath: settings.downloadPath ?? DEFAULT_SETTINGS.downloadPath,
    autoAcceptFiles: settings.autoAcceptFiles ?? DEFAULT_SETTINGS.autoAcceptFiles,
    openFolderAfterDownload: settings.openFolderAfterDownload ?? DEFAULT_SETTINGS.openFolderAfterDownload,
    e2eEnabled: settings.e2eEnabled ?? DEFAULT_SETTINGS.e2eEnabled
  }
}

function loadSettings(): QQNTSettings {
  if (typeof localStorage === 'undefined') return DEFAULT_SETTINGS
  try {
    const raw = localStorage.getItem(SETTINGS_KEY)
    if (!raw) return DEFAULT_SETTINGS
    return sanitizeSettings(JSON.parse(raw))
  } catch {
    return DEFAULT_SETTINGS
  }
}

function persistSettings(settings: QQNTSettings) {
  if (typeof localStorage === 'undefined') return
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings))
}

const initialSettings = loadSettings()

export const useUIStore = create<UIState>((set) => ({
  theme: initialSettings.theme,
  sidebarCollapsed: false,
  activeRoute: '/messages',
  settings: initialSettings,
  setTheme: (theme) =>
    set((state) => {
      const settings = sanitizeSettings({ ...state.settings, theme })
      persistSettings(settings)
      return { theme, settings }
    }),
  setActiveRoute: (route) => set({ activeRoute: route }),
  updateSettings: (patch) =>
    set((state) => {
      const settings = sanitizeSettings({ ...state.settings, ...patch })
      persistSettings(settings)
      return { settings, theme: settings.theme }
    }),
  toggleSidebar: () => set((state) => ({ sidebarCollapsed: !state.sidebarCollapsed }))
}))
