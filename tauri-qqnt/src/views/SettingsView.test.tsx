import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { open } from '@tauri-apps/plugin-dialog'
import { settingsSync } from '@/api/qqnt'
import { SettingsSidebar, SettingsView } from './SettingsView'

vi.mock('@tauri-apps/plugin-dialog', () => ({
  open: vi.fn()
}))

vi.mock('@tauri-apps/api/path', () => ({
  downloadDir: vi.fn(async () => 'C:\\Users\\jun23\\Downloads')
}))

vi.mock('@/api/qqnt', async () => {
  const actual = await vi.importActual<typeof import('@/api/qqnt')>('@/api/qqnt')
  return {
    ...actual,
    settingsSync: vi.fn(async () => ({ status: 'ok', payload: {} }))
  }
})

describe('SettingsView', () => {
  it('does not expose network host or port configuration in the client UI', () => {
    render(<SettingsSidebar active="general" onChange={vi.fn()} />)

    expect(screen.queryByText('网络')).toBeNull()
    expect(screen.queryByText('引擎地址')).toBeNull()
    expect(screen.queryByText('引擎端口')).toBeNull()
    expect(screen.queryByText('Redis')).toBeNull()
    expect(screen.queryByText('IP地址')).toBeNull()
  })

  it('uses the system folder picker for the default download directory', async () => {
    vi.mocked(open).mockResolvedValueOnce('D:\\QQDownloads')
    const promptSpy = vi.spyOn(window, 'prompt')

    render(<SettingsView />)
    fireEvent.click(screen.getByRole('button', { name: '文件' }))
    const picker = await screen.findByRole('button', { name: /Downloads|选择文件夹/ })
    fireEvent.click(picker)

    await waitFor(() => {
      expect(open).toHaveBeenCalledWith({
        directory: true,
        multiple: false,
        title: '选择默认下载目录'
      })
    })
    expect(promptSpy).not.toHaveBeenCalled()
    await waitFor(() => expect(settingsSync).toHaveBeenCalled())
  })
})
