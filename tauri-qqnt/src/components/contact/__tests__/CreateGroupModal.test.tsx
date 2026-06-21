import { describe, expect, it, beforeEach, vi } from 'vitest'
import '@testing-library/jest-dom'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { MemoryRouter } from 'react-router-dom'
import { invoke } from '@tauri-apps/api/core'
import { CreateGroupModal } from '../CreateGroupModal'
import { useContactStore } from '@/stores/contactStore'
import { useSessionStore } from '@/stores/sessionStore'
import { useUIStore } from '@/stores/uiStore'

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn()
}))

function renderWithRouter(element: React.ReactNode) {
  return render(<MemoryRouter>{element}</MemoryRouter>)
}

function seedContacts() {
  useContactStore.getState().setContacts([
    { id: 'u-1', nickname: '绿色清醒口味', avatar: '', status: 'online', remark: '清醒' },
    { id: 'u-2', nickname: '刘振博', avatar: '', status: 'offline' },
    { id: 'u-3', nickname: '秋', avatar: '', status: 'away' }
  ])
  useSessionStore.getState().setSessions([
    { id: 'u-2', type: 'private', name: '刘振博', unread: 0, pinned: false, lastTime: 3 },
    { id: 'u-1', type: 'private', name: '绿色清醒口味', unread: 0, pinned: false, lastTime: 2 }
  ])
}

describe('CreateGroupModal', () => {
  beforeEach(() => {
    useContactStore.setState({ contacts: [], groups: [], searchQuery: '', selectedContactId: null })
    useSessionStore.setState({ sessions: [], activeSessionId: null })
    useUIStore.setState({ activeRoute: '/contacts' } as Partial<ReturnType<typeof useUIStore.getState>>)
    vi.mocked(invoke).mockReset()
  })

  it('renders QQ style member-first entry without mock data', () => {
    renderWithRouter(<CreateGroupModal open onClose={vi.fn()} />)

    expect(screen.getByText('创建群聊')).toBeTruthy()
    expect(screen.getByPlaceholderText('搜索')).toBeTruthy()
    expect(screen.getByRole('button', { name: /按分类创建/ })).toBeTruthy()
    expect(screen.getByText('选择好友创建')).toBeTruthy()
    expect(screen.getByText('最近聊天')).toBeTruthy()
    expect(screen.getByText('暂无最近聊天')).toBeTruthy()
    expect(screen.getByRole('button', { name: '确定' })).toBeDisabled()
    expect(screen.queryByText('绿色清醒口味')).not.toBeInTheDocument()
  })

  it('opens category statistics page only after clicking more', () => {
    renderWithRouter(<CreateGroupModal open onClose={vi.fn()} />)

    expect(screen.queryByText('熟人与家校')).not.toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: /按分类创建/ }))

    expect(screen.getByText('按分类创建')).toBeTruthy()
    expect(screen.getByText('熟人与家校')).toBeTruthy()
    expect(screen.getByText('兴趣娱乐')).toBeTruthy()
    expect(screen.getByText('学习交流')).toBeTruthy()
    expect(screen.getByRole('button', { name: /游戏/ })).toBeTruthy()
    expect(screen.getByRole('button', { name: /品牌产品/ })).toBeTruthy()
  })

  it('selects category, fills group info and toggles agreement', () => {
    renderWithRouter(<CreateGroupModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: /按分类创建/ }))
    fireEvent.click(screen.getByRole('button', { name: /游戏/ }))
    expect(screen.getByText('填写群信息')).toBeTruthy()
    expect(screen.getByPlaceholderText('填写群名称（2-32个字）')).toBeTruthy()
    expect(screen.getByText('群分类')).toBeTruthy()
    expect(screen.getByRole('button', { name: '立即创建' })).toBeDisabled()

    fireEvent.change(screen.getByPlaceholderText('填写群名称（2-32个字）'), { target: { value: '测试游戏群' } })
    expect(screen.getByRole('button', { name: '立即创建' })).toBeEnabled()

    fireEvent.click(screen.getByLabelText('同意群聊服务声明'))
    expect(screen.getByRole('button', { name: '立即创建' })).toBeDisabled()
  })

  it('expands sub categories and supports going back', () => {
    renderWithRouter(<CreateGroupModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: /按分类创建/ }))
    fireEvent.click(screen.getByRole('button', { name: /更多兴趣/ }))
    fireEvent.click(screen.getByRole('button', { name: '影视' }))
    expect(screen.getByText('填写群信息')).toBeTruthy()
    expect(screen.getAllByRole('button', { name: '影视' }).some((button) => button.className.includes('bg-[var(--qq-primary-soft)]'))).toBe(true)

    fireEvent.click(screen.getByRole('button', { name: '上一步' }))
    expect(screen.getByText('按分类创建')).toBeTruthy()
  })

  it('filters real contacts and creates a member group', async () => {
    const onClose = vi.fn()
    seedContacts()
    vi.mocked(invoke).mockResolvedValue({
      status: 'ok',
      op: 'create_group',
      reqId: 'r1',
      payload: { groupId: 'g-member-123' }
    })

    renderWithRouter(<CreateGroupModal open onClose={onClose} />)

    fireEvent.change(screen.getByPlaceholderText('搜索'), { target: { value: '刘' } })
    expect(screen.getByText('刘振博')).toBeTruthy()
    expect(screen.queryByText('绿色清醒口味')).not.toBeInTheDocument()

    fireEvent.click(screen.getByRole('button', { name: /刘振博/ }))
    expect(screen.getByRole('button', { name: '确定' })).toBeEnabled()
    fireEvent.click(screen.getByRole('button', { name: '确定' }))

    await waitFor(() => {
      expect(invoke).toHaveBeenCalledWith(
        'qqnt_command',
        expect.objectContaining({
          payload: expect.objectContaining({
            op: 'create_group',
            payload: expect.objectContaining({ members: ['u-2'] })
          })
        })
      )
    })

    const group = useContactStore.getState().groups.find((item) => item.id === 'g-member-123')
    expect(group?.nickname).toBe('刘振博')
    expect(group?.memberCount).toBe(2)
    expect(useSessionStore.getState().activeSessionId).toBe('g-member-123')
    expect(useUIStore.getState().activeRoute).toBe('/messages')
    expect(onClose).toHaveBeenCalled()
  })

  it('calls create_group, stores category group, upserts session and closes on success', async () => {
    const onClose = vi.fn()
    vi.mocked(invoke).mockResolvedValue({
      status: 'ok',
      op: 'create_group',
      reqId: 'r1',
      payload: { groupId: 'g-test-123' }
    })

    renderWithRouter(<CreateGroupModal open onClose={onClose} />)

    fireEvent.click(screen.getByRole('button', { name: /按分类创建/ }))
    fireEvent.click(screen.getByRole('button', { name: /游戏/ }))
    fireEvent.change(screen.getByPlaceholderText('填写群名称（2-32个字）'), { target: { value: '测试群' } })
    fireEvent.click(screen.getByRole('button', { name: '立即创建' }))

    await waitFor(() => {
      expect(invoke).toHaveBeenCalledWith(
        'qqnt_command',
        expect.objectContaining({
          payload: expect.objectContaining({ op: 'create_group' })
        })
      )
    })

    const group = useContactStore.getState().groups.find((item) => item.id === 'g-test-123')
    expect(group?.category).toBe('游戏')
    expect(group?.tags).toEqual(['游戏'])
    expect(group?.avatar).toMatch(/^data:image\/svg\+xml/)
    expect(useSessionStore.getState().sessions.some((session) => session.id === 'g-test-123')).toBe(true)
    expect(useSessionStore.getState().activeSessionId).toBe('g-test-123')
    expect(useUIStore.getState().activeRoute).toBe('/messages')
    expect(onClose).toHaveBeenCalled()
  })
})
