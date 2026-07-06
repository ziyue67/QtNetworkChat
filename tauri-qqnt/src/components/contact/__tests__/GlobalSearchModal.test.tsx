import { describe, expect, it, beforeEach, vi } from 'vitest'
import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { GlobalSearchModal } from '../GlobalSearchModal'
import { useContactStore } from '@/stores/contactStore'
import type { Contact } from '@/types/qqnt'

describe('GlobalSearchModal', () => {
  beforeEach(() => {
    useContactStore.setState({ contacts: [], groups: [], searchQuery: '', selectedContactId: null })
  })

  const users: Contact[] = [
    { id: 'u1', nickname: 'Alice', status: 'online' } as Contact,
    { id: 'u2', nickname: 'Bob', status: 'offline' } as Contact
  ]

  const groups: Contact[] = [
    { id: 'g1', nickname: 'Fans群', status: 'online', memberCount: 10, category: '游戏', tags: ['游戏'] } as Contact,
    { id: 'g2', nickname: '工作群', status: 'online', memberCount: 5, category: '行业交流', tags: ['行业交流'] } as Contact
  ]

  it('renders tabs and search box', () => {
    render(<GlobalSearchModal open onClose={vi.fn()} />)
    expect(screen.getByText('综合搜索')).toBeTruthy()
    expect(screen.getByPlaceholderText('输入搜索关键词')).toBeTruthy()
    expect(screen.getByRole('button', { name: '全部' })).toBeTruthy()
    expect(screen.getByRole('button', { name: '用户' })).toBeTruthy()
    expect(screen.getByRole('button', { name: '群聊' })).toBeTruthy()
    expect(screen.getByRole('button', { name: '小程序' })).toBeTruthy()
    expect(screen.getByRole('button', { name: '机器人' })).toBeTruthy()
  })

  it('switches tabs and shows placeholder states for miniapps and bots', () => {
    render(<GlobalSearchModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: '小程序' }))
    expect(screen.getByText('小程序功能即将上线')).toBeTruthy()

    fireEvent.click(screen.getByRole('button', { name: '机器人' }))
    expect(screen.getByText('机器人功能即将上线')).toBeTruthy()
  })

  it('shows categories from existing groups and filters results when typing', () => {
    useContactStore.setState({ contacts: users, groups })
    render(<GlobalSearchModal open onClose={vi.fn()} />)

    expect(screen.getAllByRole('button', { name: '全部' }).length).toBeGreaterThan(1)
    expect(screen.getByRole('button', { name: '游戏' })).toBeTruthy()
    expect(screen.getByRole('button', { name: '行业交流' })).toBeTruthy()
    expect(screen.getByText('Fans群')).toBeTruthy()
    expect(screen.getByText('工作群')).toBeTruthy()

    fireEvent.change(screen.getByPlaceholderText('输入搜索关键词'), {
      target: { value: 'Fans' }
    })
    expect(screen.getByText('Fans群')).toBeTruthy()
    expect(screen.queryByText('工作群')).toBeNull()
  })

  it('does not render category tags when no groups exist', () => {
    render(<GlobalSearchModal open onClose={vi.fn()} />)

    expect(screen.queryByRole('button', { name: '游戏' })).toBeNull()
    expect(screen.getByText('未找到匹配群聊')).toBeTruthy()
  })

  it('filters users and groups on their tabs', () => {
    useContactStore.setState({ contacts: users, groups })
    render(<GlobalSearchModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: '用户' }))
    fireEvent.change(screen.getByPlaceholderText('输入搜索关键词'), {
      target: { value: 'Bob' }
    })
    expect(screen.getByText('Bob')).toBeTruthy()
    expect(screen.queryByText('Alice')).toBeNull()

    fireEvent.click(screen.getByRole('button', { name: '群聊' }))
    fireEvent.change(screen.getByPlaceholderText('输入搜索关键词'), {
      target: { value: '工作' }
    })
    expect(screen.getByText('工作群')).toBeTruthy()
    expect(screen.queryByText('Fans群')).toBeNull()
  })

  it('shows toast when joining a group', async () => {
    useContactStore.setState({ contacts: [], groups })
    render(<GlobalSearchModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: '群聊' }))
    const joinButtons = screen.getAllByRole('button', { name: '加入' })
    expect(joinButtons.length).toBeGreaterThan(0)
    fireEvent.click(joinButtons[0])

    await waitFor(() => {
      expect(screen.getByText('加群申请已发送')).toBeTruthy()
    })
  })

  it('opens group detail when clicking group avatar', () => {
    useContactStore.setState({ contacts: [], groups })
    render(<GlobalSearchModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByLabelText('查看 Fans群 详情'))
    expect(screen.getByText('g1（10人）')).toBeTruthy()
    expect(screen.getByText('群名称')).toBeTruthy()
    expect(screen.getByText('群分类')).toBeTruthy()
    expect(screen.getByRole('button', { name: '申请加群' })).toBeTruthy()
  })
})
