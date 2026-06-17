import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen, within } from '@testing-library/react'
import { LoginView } from './LoginView'

describe('LoginView', () => {
  it('keeps login/register client-only fields and bottom switcher', () => {
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
    expect(screen.getByLabelText('确认密码')).toBeTruthy()
  })
})
