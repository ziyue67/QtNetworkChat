import { beforeEach, describe, expect, it } from 'vitest'
import { useContactStore } from './contactStore'
import type { Contact } from '@/types/qqnt'

describe('contactStore', () => {
  beforeEach(() => {
    useContactStore.setState({ contacts: [], groups: [], searchQuery: '', selectedContactId: null })
  })

  it('normalizes backend contacts before rendering contact views', () => {
    useContactStore.getState().setContacts([
      {
        id: 123,
        nickname: '',
        status: 'weird',
        signature: 456
      } as unknown as Contact
    ])

    expect(useContactStore.getState().contacts).toEqual([
      {
        id: '123',
        nickname: '123',
        status: 'offline',
        signature: '456',
        memberCount: undefined,
        members: undefined
      }
    ])
  })

  it('normalizes group members and selected ids', () => {
    useContactStore.getState().setGroups([
      {
        id: 88,
        nickname: '测试群',
        status: 'online',
        members: [{ id: 99, nickname: '', status: 'busy' }]
      } as unknown as Contact
    ])
    useContactStore.getState().setSelectedContactId(88 as unknown as string)

    expect(useContactStore.getState().groups[0]).toMatchObject({
      id: '88',
      nickname: '测试群',
      status: 'online',
      memberCount: 1,
      members: [{ id: '99', nickname: '99', status: 'busy' }]
    })
    expect(useContactStore.getState().selectedContactId).toBe('88')
  })
})
