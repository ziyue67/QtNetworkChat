import { beforeEach, describe, expect, it } from 'vitest'
import {
  DEFAULT_CONTACT_GROUP,
  getContactGroups,
  getGroupedContacts,
  searchContacts,
  useContactStore
} from './contactStore'
import type { Contact } from '@/types/qqnt'

describe('contactStore', () => {
  beforeEach(() => {
    useContactStore.setState({
      contacts: [],
      groups: [],
      searchQuery: '',
      selectedContactId: null
    })
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
        group: DEFAULT_CONTACT_GROUP,
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
      group: DEFAULT_CONTACT_GROUP,
      memberCount: 1,
      members: [{ id: '99', nickname: '99', status: 'busy', group: DEFAULT_CONTACT_GROUP }]
    })
    expect(useContactStore.getState().selectedContactId).toBe('88')
  })

  it('defaults missing contact group to 我的好友 and preserves provided groups', () => {
    useContactStore.getState().setContacts([
      { id: 'a', nickname: 'A', status: 'online' } as Contact,
      { id: 'b', nickname: 'B', status: 'offline', group: '家人' } as Contact,
      { id: 'c', nickname: 'C', status: 'online', group: '自定义组' } as Contact
    ])
    const state = useContactStore.getState()
    expect(state.contacts[0].group).toBe(DEFAULT_CONTACT_GROUP)
    expect(state.contacts[1].group).toBe('家人')
    expect(state.contacts[2].group).toBe('自定义组')
  })

  it('returns no visible contact groups when contacts are empty', () => {
    expect(getContactGroups(useContactStore.getState())).toEqual([])
    expect(getGroupedContacts(useContactStore.getState())).toEqual({})
  })

  it('exposes grouped contact selectors with only real groups', () => {
    useContactStore.getState().setContacts([
      { id: 'a', nickname: 'A', status: 'online' } as Contact,
      { id: 'b', nickname: 'B', status: 'offline', group: '家人' } as Contact,
      { id: 'c', nickname: 'C', status: 'online', group: '自定义组' } as Contact
    ])
    const state = useContactStore.getState()
    expect(getGroupedContacts(state)[DEFAULT_CONTACT_GROUP]).toHaveLength(1)
    expect(getGroupedContacts(state)['家人']).toHaveLength(1)
    expect(getContactGroups(state)).toEqual([
      { name: DEFAULT_CONTACT_GROUP, count: 1 },
      { name: '家人', count: 1 },
      { name: '自定义组', count: 1 }
    ])
  })

  it('searches contacts by nickname, remark, account and group', () => {
    useContactStore.getState().setContacts([
      { id: 'u1', nickname: 'Alice', status: 'online', remark: '同事' } as Contact,
      { id: 'u2', nickname: 'Bob', status: 'offline', group: '同学' } as Contact
    ])
    useContactStore.getState().setGroups([
      { id: 'g1', nickname: '群A', status: 'online' } as Contact
    ])
    const state = useContactStore.getState()
    expect(searchContacts('ali')(state).friends).toHaveLength(1)
    expect(searchContacts('u2')(state).friends[0].nickname).toBe('Bob')
    expect(searchContacts('同事')(state).friends).toHaveLength(1)
    expect(searchContacts('同学')(state).friends).toHaveLength(1)
    expect(searchContacts('群A')(state).groups).toHaveLength(1)
  })
})
