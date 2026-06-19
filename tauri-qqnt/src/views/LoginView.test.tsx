import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import { invoke } from '@tauri-apps/api/core'
import { open } from '@tauri-apps/plugin-dialog'
import { LoginView } from './LoginView'

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({
    center: vi.fn(async () => undefined),
    setMinSize: vi.fn(async () => undefined),
    setResizable: vi.fn(async () => undefined),
    setSize: vi.fn(async () => undefined),
    startDragging: vi.fn(async () => undefined)
  })
}))

vi.mock('@tauri-apps/api/dpi', () => ({
  LogicalSize: class LogicalSize {
    width: number
    height: number

    constructor(width: number, height: number) {
      this.width = width
      this.height = height
    }
  }
}))

vi.mock('@tauri-apps/plugin-dialog', () => ({
  open: vi.fn()
}))

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn()
}))

describe('LoginView', () => {
  it('keeps login client-only fields and opens register as a dialog', () => {
    render(<LoginView loading={false} onLogin={vi.fn()} onRegister={vi.fn()} />)

    expect(screen.getByLabelText('账号')).toBeTruthy()
    expect(screen.getByLabelText('密码')).toBeTruthy()
    expect(screen.queryByText('Redis')).toBeNull()
    expect(screen.queryByText('IP地址')).toBeNull()
    expect(screen.queryByText('端口')).toBeNull()
    expect(screen.queryByText('服务器')).toBeNull()

    const switcher = screen.getByText('|').closest('div')
    expect(switcher).toBeTruthy()
    expect(within(switcher as HTMLElement).getByRole('button', { name: '账号密码登录' })).toBeTruthy()
    fireEvent.click(within(switcher as HTMLElement).getByRole('button', { name: '注册账号' }))

    const dialog = screen.getByRole('dialog', { name: '欢迎注册QQ' })
    expect(within(dialog).getByLabelText('账号')).toBeTruthy()
    expect(within(dialog).getByLabelText('昵称')).toBeTruthy()
    expect(within(dialog).getByLabelText('密码')).toBeTruthy()
    expect(within(dialog).getByLabelText('确认密码')).toBeTruthy()
    expect(screen.queryByLabelText('头像 URL / Base64')).toBeNull()
  })

  it('uses the native Tauri dialog to choose an avatar image', async () => {
    vi.mocked(open).mockResolvedValue('C:/tmp/avatar.png')
    vi.mocked(invoke).mockResolvedValue({ base64: 'aGVsbG8=', dataUrl: 'data:image/png;base64,aGVsbG8=' })

    render(<LoginView loading={false} onLogin={vi.fn()} onRegister={vi.fn()} />)
    fireEvent.click(screen.getByRole('button', { name: '注册账号' }))
    fireEvent.click(screen.getByRole('button', { name: '选择头像图片' }))

    await waitFor(() => {
      expect(open).toHaveBeenCalledWith(expect.objectContaining({ title: '选择头像图片' }))
      expect(invoke).toHaveBeenCalledWith('read_image_base64', { filePath: 'C:/tmp/avatar.png' })
    })

    expect(await screen.findByAltText('头像预览')).toBeTruthy()
  })

  it('submits register account password nickname and avatar', async () => {
    const onRegister = vi.fn(async () => true)
    vi.mocked(open).mockResolvedValue('C:/tmp/avatar.png')
    vi.mocked(invoke).mockResolvedValue({ base64: 'aGVsbG8=', dataUrl: 'data:image/png;base64,aGVsbG8=' })

    render(<LoginView loading={false} onLogin={vi.fn()} onRegister={onRegister} />)
    fireEvent.click(screen.getByRole('button', { name: '注册账号' }))
    fireEvent.click(screen.getByRole('button', { name: '选择头像图片' }))
    await screen.findByAltText('头像预览')

    const dialog = screen.getByRole('dialog', { name: '欢迎注册QQ' })
    fireEvent.change(within(dialog).getByLabelText('账号'), { target: { value: '10001' } })
    fireEvent.change(within(dialog).getByLabelText('昵称'), { target: { value: '小企鹅' } })
    fireEvent.change(within(dialog).getByLabelText('密码'), { target: { value: 'secret' } })
    fireEvent.change(within(dialog).getByLabelText('确认密码'), { target: { value: 'secret' } })
    fireEvent.click(within(dialog).getByRole('button', { name: '立即注册' }))

    await waitFor(() => {
      expect(onRegister).toHaveBeenCalledWith('10001', 'secret', '小企鹅', 'aGVsbG8=')
    })
  })
})
