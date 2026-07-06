import { describe, expect, it, beforeEach, vi } from 'vitest'
import '@testing-library/jest-dom'
import { fireEvent, render, screen } from '@testing-library/react'
import { FriendManagerModal } from '../FriendManagerModal'
import { DEFAULT_CONTACT_GROUP, useContactStore } from '@/stores/contactStore'
import type { Contact } from '@/types/qqnt'

describe('FriendManagerModal', () => {
  beforeEach(() => {
    useContactStore.setState({
      contacts: [],
      groups: [],
      searchQuery: '',
      selectedContactId: null
    })
  })

  const sampleContacts: Contact[] = [
    { id: 'u1', nickname: 'Alice', status: 'online', remark: '同事', group: '同事' } as Contact,
    { id: 'u2', nickname: 'Bob', status: 'offline', remark: '表哥', group: '家人' } as Contact,
    { id: 'u3', nickname: 'Carol', status: 'away', group: '同学' } as Contact,
    { id: 'u4', nickname: 'Dave', status: 'busy' } as Contact
  ]

  it('renders only real groups and counts', () => {
    useContactStore.setState({ contacts: sampleContacts })
    render(<FriendManagerModal open onClose={vi.fn()} />)

    expect(screen.getByRole('button', { name: /全部好友4/ })).toBeTruthy()
    expect(screen.getByRole('button', { name: new RegExp(`${DEFAULT_CONTACT_GROUP}1`) })).toBeTruthy()
    expect(screen.getByRole('button', { name: /家人1/ })).toBeTruthy()
    expect(screen.getByRole('button', { name: /同学1/ })).toBeTruthy()
    expect(screen.getByRole('button', { name: /同事1/ })).toBeTruthy()
    expect(screen.queryByRole('button', { name: /朋友0/ })).toBeNull()
  })

  it('hides default preset groups when contacts have none', () => {
    render(<FriendManagerModal open onClose={vi.fn()} />)

    expect(screen.queryByRole('button', { name: /我的好友0/ })).toBeNull()
    expect(screen.queryByRole('button', { name: /朋友0/ })).toBeNull()
    expect(screen.queryByRole('button', { name: /公会的人0/ })).toBeNull()
  })

  it('filters contacts by selected group', () => {
    useContactStore.setState({ contacts: sampleContacts })
    render(<FriendManagerModal open onClose={vi.fn()} />)

    fireEvent.click(screen.getByRole('button', { name: /家人1/ }))
    expect(screen.getByText('Bob')).toBeTruthy()
    expect(screen.queryByText('Alice')).toBeNull()

    fireEvent.click(screen.getByRole('button', { name: new RegExp(`${DEFAULT_CONTACT_GROUP}1`) }))
    expect(screen.getByText('Dave')).toBeTruthy()
  })

  it('filters contacts by search query', () => {
    useContactStore.setState({ contacts: sampleContacts })
    render(<FriendManagerModal open onClose={vi.fn()} />)

    fireEvent.change(screen.getByPlaceholderText('搜索'), { target: { value: '同事' } })
    expect(screen.getByText('Alice')).toBeTruthy()
    expect(screen.queryByText('Bob')).toBeNull()

    fireEvent.change(screen.getByPlaceholderText('搜索'), { target: { value: 'u2' } })
    expect(screen.getByText('Bob')).toBeTruthy()
    expect(screen.queryByText('Alice')).toBeNull()
  })

  it('selects individual rows and toggles select all', () => {
    useContactStore.setState({ contacts: sampleContacts })
    render(<FriendManagerModal open onClose={vi.fn()} />)

    const checkboxes = screen.getAllByRole('checkbox')
    expect(checkboxes.length).toBeGreaterThan(1)

    fireEvent.click(checkboxes[1])
    expect(checkboxes[1]).toBeChecked()

    fireEvent.click(checkboxes[0])
    for (const checkbox of checkboxes.slice(1)) {
      expect(checkbox).toBeChecked()
    }

    fireEvent.click(checkboxes[0])
    for (const checkbox of checkboxes.slice(1)) {
      expect(checkbox).not.toBeChecked()
    }
  })

  it('shows disabled add group placeholder', () => {
    render(<FriendManagerModal open onClose={vi.fn()} />)
    expect(screen.getByRole('button', { name: /添加分组/ })).toBeDisabled()
  })

  it('closes on Escape key', () => {
    const onClose = vi.fn()
    render(<FriendManagerModal open onClose={onClose} />)
    fireEvent.keyDown(document, { key: 'Escape' })
    expect(onClose).toHaveBeenCalled()
  })
})
