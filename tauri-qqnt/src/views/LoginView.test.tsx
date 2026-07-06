import { describe, expect, it, vi, beforeEach } from 'vitest'
import '@testing-library/jest-dom'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { LoginView, REGISTER_HINT_KEY } from './LoginView'

const { webviewWindowMock } = vi.hoisted(() => ({ webviewWindowMock: vi.fn() }))

vi.mock('@tauri-apps/api/webviewWindow', () => ({
  WebviewWindow: webviewWindowMock
}))

vi.mock('@tauri-apps/api/window', () => ({
  getCurrentWindow: () => ({
    center: vi.fn(async () => undefined),
    setMinSize: vi.fn(async () => undefined),
    setSize: vi.fn(async () => undefined),
    startDragging: vi.fn(async () => undefined)
  })
}))

describe('LoginView', () => {
  beforeEach(() => {
    webviewWindowMock.mockClear()
    localStorage.clear()
  })

  it('renders QQ login form', () => {
    render(<LoginView loading={false} onLogin={vi.fn()} />)

    expect(screen.getByText('QQ')).toBeTruthy()
    expect(screen.getByPlaceholderText('请输入账号 / 手机号')).toBeTruthy()
    expect(screen.getByPlaceholderText('请输入密码')).toBeTruthy()
    expect(screen.getByRole('button', { name: '注册账号' })).toBeTruthy()
  })

  it('requires agreement before enabling login', () => {
    render(<LoginView loading={false} onLogin={vi.fn()} />)

    fireEvent.change(screen.getByPlaceholderText('请输入账号 / 手机号'), { target: { value: '10001' } })
    fireEvent.change(screen.getByPlaceholderText('请输入密码'), { target: { value: 'secret' } })
    expect(screen.getByRole('button', { name: '登 录' })).toBeDisabled()

    fireEvent.click(screen.getByRole('checkbox', { name: /已阅读并同意/ }))
    expect(screen.getByRole('button', { name: '登 录' })).toBeEnabled()
  })

  it('submits account and password after agreement', async () => {
    const onLogin = vi.fn()
    render(<LoginView loading={false} onLogin={onLogin} />)

    fireEvent.change(screen.getByPlaceholderText('请输入账号 / 手机号'), { target: { value: '10001' } })
    fireEvent.change(screen.getByPlaceholderText('请输入密码'), { target: { value: 'secret' } })
    fireEvent.click(screen.getByRole('checkbox', { name: /已阅读并同意/ }))
    fireEvent.click(screen.getByRole('button', { name: '登 录' }))

    await waitFor(() => {
      expect(onLogin).toHaveBeenCalledWith('10001', 'secret')
    })
  })

  it('opens register in a separate window', async () => {
    render(<LoginView loading={false} onLogin={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: '注册账号' }))

    await waitFor(() => {
      expect(webviewWindowMock).toHaveBeenCalledWith(
        'register-account',
        expect.objectContaining({
          url: 'index.html#/register',
          title: '注册账号',
          width: 480,
          height: 680
        })
      )
    })
  })

  it('prefills account and shows hint after register window stored account', () => {
    localStorage.setItem(REGISTER_HINT_KEY, '20002')
    render(<LoginView loading={false} onLogin={vi.fn()} />)

    expect((screen.getByPlaceholderText('请输入账号 / 手机号') as HTMLInputElement).value).toBe('20002')
    expect(screen.getByText('注册成功，请登录')).toBeInTheDocument()
    expect(localStorage.getItem(REGISTER_HINT_KEY)).toBeNull()
  })
})
