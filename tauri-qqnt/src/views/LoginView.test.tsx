import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen, waitFor, within } from '@testing-library/react'
import { invoke } from '@tauri-apps/api/core'
import { open } from '@tauri-apps/plugin-dialog'
import { LoginView } from './LoginView'

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({
    center: vi.fn(async () => undefined),
    setMinSize: vi.fn(async () => undefined),
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
  it('keeps login client-only fields and opens a large register window', () => {
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
    expect(within(switcher as HTMLElement).getByRole('button', { name: '注册账号' })).toBeTruthy()

    fireEvent.click(screen.getByRole('button', { name: '注册账号' }))
    expect(screen.getByRole('heading', { name: '欢迎注册QQ' })).toBeTruthy()
    expect(screen.getByPlaceholderText('请输入账号 / 手机号')).toBeTruthy()
    expect(screen.getByPlaceholderText('请输入昵称')).toBeTruthy()
    expect(screen.getByPlaceholderText('请设置 QQ 密码')).toBeTruthy()
    expect(screen.getByPlaceholderText('请再次输入密码')).toBeTruthy()
    expect(screen.queryByPlaceholderText('可选：粘贴头像 URL 或 Base64')).toBeNull()
    expect(screen.queryByLabelText('头像 URL / Base64')).toBeNull()
    expect(screen.getByRole('button', { name: '选择头像图片' })).toBeTruthy()

    fireEvent.click(screen.getByRole('button', { name: '关闭注册' }))
    expect(screen.getByRole('button', { name: '账号密码登录' })).toBeTruthy()
  })

  it('chooses avatar through the native Tauri dialog', async () => {
    vi.mocked(open).mockResolvedValue('C:/tmp/avatar.png')
    vi.mocked(invoke).mockResolvedValue({ base64: 'aGVsbG8=', dataUrl: 'data:image/png;base64,aGVsbG8=' })

    render(<LoginView loading={false} onLogin={vi.fn()} onRegister={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: '注册账号' }))
    fireEvent.click(screen.getByRole('button', { name: '选择头像图片' }))

    await waitFor(() => {
      expect(open).toHaveBeenCalledWith(expect.objectContaining({ title: '选择头像图片' }))
      expect(invoke).toHaveBeenCalledWith('read_image_base64', { filePath: 'C:/tmp/avatar.png' })
    })
  })

  it('submits account password nickname and selected avatar from register dialog', async () => {
    const onRegister = vi.fn(async () => true)
    vi.mocked(open).mockResolvedValue('C:/tmp/avatar.png')
    vi.mocked(invoke).mockResolvedValue({ base64: 'aGVsbG8=', dataUrl: 'data:image/png;base64,aGVsbG8=' })

    render(<LoginView loading={false} onLogin={vi.fn()} onRegister={onRegister} />)

    fireEvent.click(screen.getByRole('button', { name: '注册账号' }))
    fireEvent.click(screen.getByRole('button', { name: '选择头像图片' }))
    await waitFor(() => expect(invoke).toHaveBeenCalled())
    fireEvent.change(screen.getByPlaceholderText('请输入账号 / 手机号'), { target: { value: '10001' } })
    fireEvent.change(screen.getByPlaceholderText('请输入昵称'), { target: { value: '阿明' } })
    fireEvent.change(screen.getByPlaceholderText('请设置 QQ 密码'), { target: { value: 'secret' } })
    fireEvent.change(screen.getByPlaceholderText('请再次输入密码'), { target: { value: 'secret' } })
    fireEvent.click(screen.getByRole('button', { name: '立即注册' }))

    await waitFor(() => {
      expect(onRegister).toHaveBeenCalledWith({
        account: '10001',
        password: 'secret',
        nickname: '阿明',
        avatar: 'data:image/png;base64,aGVsbG8=',
        avatarBase64: 'aGVsbG8='
      })
    })
  })
})
