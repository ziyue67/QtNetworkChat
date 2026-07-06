import { create } from 'zustand'
import type { ThemeMode } from '@/types/qqnt'
import { DEFAULT_SCREENSHOT_SHORTCUT, normalizeShortcut } from '@/lib/shortcut'

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
  screenshotShortcut: string
  hideWindowBeforeScreenshot: boolean
}

interface UIState {
  theme: ThemeMode
  sidebarCollapsed: boolean
  activeRoute: string
  settings: QQNTSettings
  setTheme: (theme: ThemeMode) => void
  setActiveRoute: (route: string) => void
  updateSettings: (patch: Partial<QQNTSettings>) => void
  ensureDefaultDownloadPath: (resolver?: () => Promise<string>) => Promise<string>
  toggleSidebar: () => void
}

const SETTINGS_KEY = 'qqnt:settings'
const SETTINGS_VERSION = 3
export const DEFAULT_DOWNLOAD_FOLDER_NAME = 'qq_net'
export const FALLBACK_DOWNLOAD_PATH = `C:\\Users\\user\\Downloads\\${DEFAULT_DOWNLOAD_FOLDER_NAME}`

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
  e2eEnabled: true,
  screenshotShortcut: DEFAULT_SCREENSHOT_SHORTCUT,
  hideWindowBeforeScreenshot: false
}

type PersistedSettings = Partial<QQNTSettings> & {
  __version?: number
}

function sanitizeSettings(settings: Partial<QQNTSettings>, options: { resetLegacyScreenshotHide?: boolean } = {}): QQNTSettings {
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
    e2eEnabled: settings.e2eEnabled ?? DEFAULT_SETTINGS.e2eEnabled,
    screenshotShortcut: normalizeShortcut(settings.screenshotShortcut),
    hideWindowBeforeScreenshot: options.resetLegacyScreenshotHide
      ? DEFAULT_SETTINGS.hideWindowBeforeScreenshot
      : settings.hideWindowBeforeScreenshot ?? DEFAULT_SETTINGS.hideWindowBeforeScreenshot
  }
}

function loadSettings(): QQNTSettings {
  if (typeof localStorage === 'undefined') return DEFAULT_SETTINGS
  try {
    const raw = localStorage.getItem(SETTINGS_KEY)
    if (!raw) return DEFAULT_SETTINGS
    const persisted = JSON.parse(raw) as PersistedSettings
    const settings = sanitizeSettings(persisted, {
      resetLegacyScreenshotHide: (persisted.__version ?? 0) < SETTINGS_VERSION
    })
    persistSettings(settings)
    return settings
  } catch {
    return DEFAULT_SETTINGS
  }
}

function persistSettings(settings: QQNTSettings) {
  if (typeof localStorage === 'undefined') return
  localStorage.setItem(SETTINGS_KEY, JSON.stringify({ ...settings, __version: SETTINGS_VERSION }))
}

export function toClientDownloadPath(downloadRoot: string) {
  const root = downloadRoot.trim().replace(/[\\/]+$/, '')
  if (!root) return FALLBACK_DOWNLOAD_PATH
  const leaf = root.split(/[\\/]/).filter(Boolean).pop()
  return leaf?.toLowerCase() === DEFAULT_DOWNLOAD_FOLDER_NAME ? root : `${root}\\${DEFAULT_DOWNLOAD_FOLDER_NAME}`
}

function shouldUpgradeLegacyDownloadPath(path: string) {
  const leaf = path.trim().replace(/[\\/]+$/, '').split(/[\\/]/).filter(Boolean).pop()
  return leaf?.toLowerCase() === 'downloads'
}

export async function resolveDefaultDownloadPath() {
  try {
    const { downloadDir } = await import('@tauri-apps/api/path')
    const resolved = await downloadDir()
    if (resolved.trim()) return toClientDownloadPath(resolved)
  } catch {
    // Fall through to browser/test-friendly defaults.
  }

  const env = globalThis as typeof globalThis & { process?: { env?: Record<string, string | undefined> } }
  const home = env.process?.env?.USERPROFILE || env.process?.env?.HOME
  return home ? toClientDownloadPath(`${home.replace(/[\\/]+$/, '')}\\Downloads`) : FALLBACK_DOWNLOAD_PATH
}

const initialSettings = loadSettings()

export const useUIStore = create<UIState>((set, get) => ({
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
  ensureDefaultDownloadPath: async (resolver = resolveDefaultDownloadPath) => {
    const current = get().settings.downloadPath.trim()
    if (current) {
      if (!shouldUpgradeLegacyDownloadPath(current)) return current
      const upgraded = toClientDownloadPath(current)
      get().updateSettings({ downloadPath: upgraded })
      return upgraded
    }

    const downloadPath = await resolver()
    get().updateSettings({ downloadPath })
    return downloadPath
  },
  toggleSidebar: () => set((state) => ({ sidebarCollapsed: !state.sidebarCollapsed }))
}))
