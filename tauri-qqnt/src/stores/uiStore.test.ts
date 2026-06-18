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
})
