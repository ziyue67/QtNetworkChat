import { describe, expect, it, vi, beforeEach } from 'vitest'
import '@testing-library/jest-dom'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import { RegisterWindow } from './RegisterWindow'
import { register, connectServer } from '@/api/qqnt'

const windowMocks = vi.hoisted(() => ({
  close: vi.fn(async () => undefined),
  startDragging: vi.fn(async () => undefined)
}))

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn()
}))

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({
    close: windowMocks.close,
    startDragging: windowMocks.startDragging
  })
}))

vi.mock('@tauri-apps/plugin-dialog', () => ({
  open: vi.fn()
}))

vi.mock('@/api/qqnt', async () => {
  const actual = await vi.importActual<typeof import('@/api/qqnt')>('@/api/qqnt')
  return {
    ...actual,
    register: vi.fn(),
    connectServer: vi.fn()
  }
})

describe('RegisterWindow', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    localStorage.clear()
  })

  function renderWindow() {
    render(
      <MemoryRouter initialEntries={['/register']}>
        <Routes>
          <Route path="/register" element={<RegisterWindow />} />
        </Routes>
      </MemoryRouter>
    )
  }

  it('renders register form and agreement checkbox', () => {
    renderWindow()

    expect(screen.getByText('欢迎注册QQ')).toBeTruthy()
    expect(screen.getByRole('heading', { name: '创建新账号' })).toBeTruthy()
    expect(screen.getByPlaceholderText('请输入账号 / 手机号')).toBeTruthy()
    expect(screen.getByPlaceholderText('请输入昵称')).toBeTruthy()
    expect(screen.getByPlaceholderText('请设置 QQ 密码')).toBeTruthy()
    expect(screen.getByPlaceholderText('请再次输入密码')).toBeTruthy()
    expect(screen.getByRole('button', { name: '立即注册' })).toBeDisabled()
  })

  it('supports dragging the title area and closing the window', () => {
    renderWindow()

    fireEvent.mouseDown(screen.getByText('欢迎注册QQ'), { button: 0 })
    expect(windowMocks.startDragging).toHaveBeenCalledTimes(1)

    fireEvent.click(screen.getByRole('button', { name: '关闭' }))
    expect(windowMocks.close).toHaveBeenCalledTimes(1)
  })

  it('enables submit only after all fields filled and agreement checked', () => {
    renderWindow()

    fireEvent.change(screen.getByPlaceholderText('请输入账号 / 手机号'), { target: { value: '10001' } })
    fireEvent.change(screen.getByPlaceholderText('请输入昵称'), { target: { value: '阿明' } })
    fireEvent.change(screen.getByPlaceholderText('请设置 QQ 密码'), { target: { value: 'secret' } })
    fireEvent.change(screen.getByPlaceholderText('请再次输入密码'), { target: { value: 'secret' } })
    expect(screen.getByRole('button', { name: '立即注册' })).toBeDisabled()

    fireEvent.click(screen.getByRole('checkbox'))
    expect(screen.getByRole('button', { name: '立即注册' })).toBeEnabled()
  })

  it('shows error when passwords do not match', async () => {
    renderWindow()

    fireEvent.change(screen.getByPlaceholderText('请输入账号 / 手机号'), { target: { value: '10001' } })
    fireEvent.change(screen.getByPlaceholderText('请输入昵称'), { target: { value: '阿明' } })
    fireEvent.change(screen.getByPlaceholderText('请设置 QQ 密码'), { target: { value: 'secret' } })
    fireEvent.change(screen.getByPlaceholderText('请再次输入密码'), { target: { value: 'different' } })
    fireEvent.click(screen.getByRole('checkbox'))
    fireEvent.click(screen.getByRole('button', { name: '立即注册' }))

    expect(await screen.findByText('两次输入的密码不一致')).toBeInTheDocument()
  })

  it('submits register and connects server when agreement is checked', async () => {
    vi.mocked(register).mockResolvedValue({
      status: 'ok',
      reqId: 'r1',
      payload: { accepted: true, requiresConnect: true }
    } as unknown as Awaited<ReturnType<typeof register>>)
    vi.mocked(connectServer).mockResolvedValue({
      status: 'ok',
      reqId: 'c1',
      payload: { connected: true, host: '127.0.0.1', port: 8888 }
    } as unknown as Awaited<ReturnType<typeof connectServer>>)

    renderWindow()

    fireEvent.change(screen.getByPlaceholderText('请输入账号 / 手机号'), { target: { value: '10001' } })
    fireEvent.change(screen.getByPlaceholderText('请输入昵称'), { target: { value: '阿明' } })
    fireEvent.change(screen.getByPlaceholderText('请设置 QQ 密码'), { target: { value: 'secret' } })
    fireEvent.change(screen.getByPlaceholderText('请再次输入密码'), { target: { value: 'secret' } })
    fireEvent.click(screen.getByRole('checkbox'))
    fireEvent.click(screen.getByRole('button', { name: '立即注册' }))

    await waitFor(() => {
      expect(register).toHaveBeenCalledWith('10001', 'secret', '阿明')
      expect(connectServer).toHaveBeenCalledWith('127.0.0.1', 8888)
    })
  })
})
