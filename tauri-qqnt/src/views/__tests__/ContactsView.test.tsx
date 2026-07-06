import { describe, expect, it, beforeEach, vi } from 'vitest'
import '@testing-library/jest-dom'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { MemoryRouter } from 'react-router-dom'
import { ContactsView } from '../ContactsView'
import { useContactStore } from '@/stores/contactStore'
import { useSessionStore } from '@/stores/sessionStore'
import type { Contact } from '@/types/qqnt'

const { webviewWindowMock } = vi.hoisted(() => ({ webviewWindowMock: vi.fn() }))

vi.mock('@tauri-apps/api/webviewWindow', () => ({
  WebviewWindow: webviewWindowMock
}))

function renderWithRouter(element: React.ReactNode) {
  return render(<MemoryRouter>{element}</MemoryRouter>)
}

describe('ContactsView', () => {
  beforeEach(() => {
    webviewWindowMock.mockClear()
    useContactStore.setState({ contacts: [], groups: [], searchQuery: '', selectedContactId: null })
    useSessionStore.setState({ sessions: [], activeSessionId: null })
  })

  it('renders search box, plus menu, manager entry, notice entries and tabs', () => {
    renderWithRouter(<ContactsView />)

    expect(screen.getByPlaceholderText('搜索')).toBeTruthy()
    expect(screen.getByLabelText('更多')).toBeTruthy()
    expect(screen.getByText('好友管理器')).toBeTruthy()
    expect(screen.getByText('好友通知')).toBeTruthy()
    expect(screen.getByText('群通知')).toBeTruthy()
    expect(screen.getByRole('button', { name: '好友' })).toBeTruthy()
    expect(screen.getByRole('button', { name: '群聊' })).toBeTruthy()
  })

  it('shows notice red dots only when matching unread messages exist', () => {
    const { unmount } = renderWithRouter(<ContactsView />)
    expect(screen.getByTestId('friend-notice-dot')).toHaveClass('hidden')
    expect(screen.getByTestId('group-notice-dot')).toHaveClass('hidden')

    unmount()
    useSessionStore.setState({
      sessions: [
        { id: 'u1', type: 'private', name: 'Alice', unread: 1, pinned: false },
        { id: 'g1', type: 'group', name: 'Group', unread: 2, pinned: false }
      ],
      activeSessionId: null
    })
    renderWithRouter(<ContactsView />)

    expect(screen.getByTestId('friend-notice-dot')).not.toHaveClass('hidden')
    expect(screen.getByTestId('group-notice-dot')).not.toHaveClass('hidden')
  })

  it('opens GlobalSearchModal when search input is focused', () => {
    renderWithRouter(<ContactsView />)

    fireEvent.focus(screen.getByPlaceholderText('搜索'))
    expect(screen.getByText('综合搜索')).toBeTruthy()
  })

  it('opens plus menu and can create group / open search', () => {
    renderWithRouter(<ContactsView />)

    fireEvent.click(screen.getByLabelText('更多'))
    expect(screen.getByText('创建群聊')).toBeTruthy()
    expect(screen.getByText('加好友/群')).toBeTruthy()
    expect(screen.getByText('闪传文件')).toBeDisabled()

    fireEvent.click(screen.getByText('创建群聊'))
    expect(screen.getByText('按分类创建')).toBeTruthy()
    expect(screen.getByText('选择分类')).toBeTruthy()

    fireEvent.click(screen.getByLabelText('关闭'))
    expect(screen.queryByPlaceholderText('群名称（必填）')).toBeNull()

    fireEvent.click(screen.getByLabelText('更多'))
    fireEvent.click(screen.getByText('加好友/群'))
    expect(screen.getByText('综合搜索')).toBeTruthy()
  })

  it('opens FriendManagerModal from manager entry', () => {
    renderWithRouter(<ContactsView />)

    fireEvent.click(screen.getByText('好友管理器'))
    expect(screen.getAllByText('好友管理器')).toHaveLength(2)
  })

  it('opens friend and group notice panels from notice entries', () => {
    renderWithRouter(<ContactsView />)

    fireEvent.click(screen.getByText('好友通知'))
    expect(screen.getByRole('heading', { name: '好友通知' })).toBeTruthy()
    expect(screen.queryByRole('dialog', { name: '好友通知' })).not.toBeInTheDocument()
    expect(screen.getByText('暂无通知')).toBeTruthy()

    fireEvent.click(screen.getByText('群通知'))
    expect(screen.getByRole('heading', { name: '群通知' })).toBeTruthy()
  })

  it('opens filtered notice in a separate window', async () => {
    renderWithRouter(<ContactsView />)

    fireEvent.click(screen.getByText('好友通知'))
    fireEvent.click(screen.getByLabelText('筛选通知'))

    await waitFor(() => {
      expect(webviewWindowMock).toHaveBeenCalledWith(
        expect.stringMatching(/^notice-filter-friend-/),
        expect.objectContaining({
          url: 'index.html#/notice-filter?type=friend',
          title: '已过滤的通知',
          width: 420,
          height: 520
        })
      )
    })
    expect(screen.queryByRole('heading', { name: '已过滤的通知' })).not.toBeInTheDocument()

    fireEvent.click(screen.getByText('群通知'))
    fireEvent.click(screen.getByLabelText('筛选通知'))

    await waitFor(() => {
      expect(webviewWindowMock).toHaveBeenCalledWith(
        expect.stringMatching(/^notice-filter-group-/),
        expect.objectContaining({
          url: 'index.html#/notice-filter?type=group',
          title: '已过滤的通知'
        })
      )
    })
  })

  it('switches between friends and groups tabs', () => {
    useContactStore.setState({
      contacts: [
        { id: 'u1', nickname: 'Alice', status: 'online' } as Contact,
        { id: 'u2', nickname: 'Bob', status: 'offline' } as Contact
      ],
      groups: [{ id: 'g1', nickname: 'GroupA', status: 'online', memberCount: 3 } as Contact]
    })

    renderWithRouter(<ContactsView />)

    expect(screen.getByText('好友 (2)')).toBeTruthy()
    fireEvent.click(screen.getByRole('button', { name: '群聊' }))
    expect(screen.getByText('群聊 (1)')).toBeTruthy()
  })
})
