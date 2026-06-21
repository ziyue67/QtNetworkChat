import { beforeEach, describe, expect, it, vi } from 'vitest'

const SETTINGS_KEY = 'qqnt:settings'

describe('uiStore settings', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.resetModules()
  })

  it('keeps network host and port out of default client preferences', async () => {
    const { DEFAULT_SETTINGS } = await import('./uiStore')

    expect(DEFAULT_SETTINGS).not.toHaveProperty('networkHost')
    expect(DEFAULT_SETTINGS).not.toHaveProperty('networkPort')
  })

  it('strips legacy network settings when loading persisted preferences', async () => {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify({
      theme: 'dark',
      networkHost: '192.168.1.20',
      networkPort: 6379
    }))

    const { useUIStore } = await import('./uiStore')

    expect(useUIStore.getState().settings).toMatchObject({ theme: 'dark' })
    expect(useUIStore.getState().settings).not.toHaveProperty('networkHost')
    expect(useUIStore.getState().settings).not.toHaveProperty('networkPort')
  })

  it('resets legacy screenshot hide preference to unchecked', async () => {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify({
      hideWindowBeforeScreenshot: true
    }))

    const { useUIStore } = await import('./uiStore')

    expect(useUIStore.getState().settings.hideWindowBeforeScreenshot).toBe(false)
    expect(JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}').hideWindowBeforeScreenshot).toBe(false)
  })

  it('keeps a newly chosen screenshot hide preference', async () => {
    const { useUIStore } = await import('./uiStore')

    useUIStore.getState().updateSettings({ hideWindowBeforeScreenshot: true })
    vi.resetModules()
    const { useUIStore: reloadedStore } = await import('./uiStore')

    expect(reloadedStore.getState().settings.hideWindowBeforeScreenshot).toBe(true)
  })

  it('strips legacy network settings before persisting preference updates', async () => {
    const { useUIStore } = await import('./uiStore')

    useUIStore.getState().updateSettings({
      theme: 'dark',
      networkHost: '10.0.0.2',
      networkPort: 16000
    } as Partial<ReturnType<typeof useUIStore.getState>['settings']>)

    const stored = JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}')

    expect(useUIStore.getState().settings).not.toHaveProperty('networkHost')
    expect(useUIStore.getState().settings).not.toHaveProperty('networkPort')
    expect(stored).not.toHaveProperty('networkHost')
    expect(stored).not.toHaveProperty('networkPort')
  })

  it('initializes an empty download path under the client downloads folder', async () => {
    const { toClientDownloadPath, useUIStore } = await import('./uiStore')

    const defaultPath = toClientDownloadPath('C:\\Users\\jun23\\Downloads')
    await useUIStore.getState().ensureDefaultDownloadPath(async () => defaultPath)

    expect(useUIStore.getState().settings.downloadPath).toBe('C:\\Users\\jun23\\Downloads\\qq_net')
    expect(JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}').downloadPath).toBe('C:\\Users\\jun23\\Downloads\\qq_net')
  })

  it('does not append the client downloads folder twice', async () => {
    const { toClientDownloadPath } = await import('./uiStore')

    expect(toClientDownloadPath('C:\\Users\\jun23\\Downloads\\qq_net')).toBe('C:\\Users\\jun23\\Downloads\\qq_net')
  })

  it('does not overwrite a manually configured download path', async () => {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify({ downloadPath: 'D:\\QQDownloads' }))
    const { useUIStore } = await import('./uiStore')

    const result = await useUIStore.getState().ensureDefaultDownloadPath(async () => 'C:\\Users\\jun23\\Downloads')

    expect(result).toBe('D:\\QQDownloads')
    expect(useUIStore.getState().settings.downloadPath).toBe('D:\\QQDownloads')
  })

  it('upgrades the legacy bare downloads folder to the client folder', async () => {
    localStorage.setItem(SETTINGS_KEY, JSON.stringify({ downloadPath: 'C:\\Users\\jun23\\Downloads' }))
    const { useUIStore } = await import('./uiStore')

    const result = await useUIStore.getState().ensureDefaultDownloadPath()

    expect(result).toBe('C:\\Users\\jun23\\Downloads\\qq_net')
    expect(useUIStore.getState().settings.downloadPath).toBe('C:\\Users\\jun23\\Downloads\\qq_net')
  })
})
