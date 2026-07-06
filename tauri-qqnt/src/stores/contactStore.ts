import { create } from 'zustand'
import type { Contact } from '@/types/qqnt'

const VALID_STATUSES = new Set<Contact['status']>(['online', 'offline', 'busy', 'away'])

export const ALL_FRIENDS_GROUP = '全部好友'
export const DEFAULT_CONTACT_GROUP = '我的好友'
export const DEFAULT_CONTACT_GROUPS = ['我的好友', '朋友', '家人', '同学', '公会的人'] as const
const DEFAULT_CONTACT_GROUP_SET = new Set<string>(DEFAULT_CONTACT_GROUPS as unknown as string[])

export interface ContactGroupInfo {
  name: string
  count: number
}

function sortExtraGroups(names: string[]): string[] {
  return [...names].sort((left, right) => left.localeCompare(right, 'zh-Hans-CN'))
}

export function getNormalizedGroup(contact: Contact): string {
  return contact.group?.trim() || DEFAULT_CONTACT_GROUP
}

export function getContactGroupCounts(contacts: Contact[]): Record<string, number> {
  const counts: Record<string, number> = {}
  for (const contact of contacts) {
    const group = getNormalizedGroup(contact)
    counts[group] = (counts[group] ?? 0) + 1
  }
  return counts
}

export function getContactsByGroup(contacts: Contact[], group: string): Contact[] {
  if (group === ALL_FRIENDS_GROUP) return contacts
  return contacts.filter((contact) => getNormalizedGroup(contact) === group)
}

export const getGroupedContacts = (state: ContactState): Record<string, Contact[]> => {
  const grouped: Record<string, Contact[]> = {}
  for (const contact of state.contacts) {
    const group = getNormalizedGroup(contact)
    grouped[group] = grouped[group] ?? []
    grouped[group].push(contact)
  }
  return grouped
}

export const getContactGroups = (state: ContactState): ContactGroupInfo[] => {
  const counts = getContactGroupCounts(state.contacts)
  const presetGroups = DEFAULT_CONTACT_GROUPS.filter((name) => counts[name] !== undefined).map((name) => ({
    name,
    count: counts[name] ?? 0
  }))
  const extraGroups = sortExtraGroups(
    Object.keys(counts).filter((name) => !DEFAULT_CONTACT_GROUP_SET.has(name))
  ).map((name) => ({ name, count: counts[name] ?? 0 }))
  return [...presetGroups, ...extraGroups]
}

export const searchContacts = (query: string) => {
  return (state: ContactState) => {
    const q = query.trim().toLowerCase()
    if (!q) return { friends: state.contacts, groups: state.groups }
    const matches = (contact: Contact) =>
      contact.nickname.toLowerCase().includes(q) ||
      contact.id.toLowerCase().includes(q) ||
      (contact.remark && contact.remark.toLowerCase().includes(q)) ||
      (contact.group && contact.group.toLowerCase().includes(q)) ||
      (contact.signature && contact.signature.toLowerCase().includes(q))
    return { friends: state.contacts.filter(matches), groups: state.groups.filter(matches) }
  }
}

function text(value: unknown, fallback = '') {
  if (typeof value === 'string') return value.trim() || fallback
  if (typeof value === 'number' || typeof value === 'bigint') return String(value)
  return fallback
}

function normalizeStatus(value: unknown): Contact['status'] {
  return typeof value === 'string' && VALID_STATUSES.has(value as Contact['status'])
    ? (value as Contact['status'])
    : 'offline'
}

function normalizeContact(contact: Contact): Contact | null {
  const raw = contact as unknown as Record<string, unknown>
  const id = text(raw.id)
  if (!id) return null

  const rawMembers = Array.isArray(raw.members) ? raw.members : []
  const members = rawMembers
    .map((member) => normalizeContact(member as Contact))
    .filter((member): member is Contact => Boolean(member))
  const memberCount = Number(raw.memberCount)
  const rawTags = Array.isArray(raw.tags) ? raw.tags : []
  const tags = rawTags
    .map((tag) => text(tag))
    .filter((tag) => Boolean(tag))

  return {
    ...contact,
    id,
    nickname: text(raw.nickname, id),
    avatar: text(raw.avatar) || undefined,
    status: normalizeStatus(raw.status),
    signature: text(raw.signature) || undefined,
    remark: text(raw.remark) || undefined,
    group: text(raw.group, DEFAULT_CONTACT_GROUP),
    category: text(raw.category) || undefined,
    tags: tags.length > 0 ? tags : undefined,
    announcement: text(raw.announcement) || undefined,
    memberCount: Number.isFinite(memberCount) ? memberCount : members.length || undefined,
    members: members.length > 0 ? members : undefined
  }
}

function normalizeContacts(contacts: Contact[]) {
  return contacts
    .map((contact) => normalizeContact(contact))
    .filter((contact): contact is Contact => Boolean(contact))
}

export interface ContactState {
  contacts: Contact[]
  groups: Contact[]
  searchQuery: string
  selectedContactId: string | null
  setContacts: (contacts: Contact[]) => void
  setGroups: (groups: Contact[]) => void
  addContact: (contact: Contact) => void
  addGroup: (group: Contact) => void
  updatePresence: (id: string, status: Contact['status']) => void
  setSearchQuery: (q: string) => void
  setSelectedContactId: (id: string | null) => void
}

export const useContactStore = create<ContactState>((set) => ({
  contacts: [],
  groups: [],
  searchQuery: '',
  selectedContactId: null,
  setContacts: (contacts) => set({ contacts: normalizeContacts(contacts) }),
  setGroups: (groups) => set({ groups: normalizeContacts(groups) }),
  addContact: (contact) =>
    set((state) => {
      const normalized = normalizeContact(contact)
      if (!normalized || state.contacts.some((c) => c.id === normalized.id)) return state
      return { contacts: [...state.contacts, normalized] }
    }),
  addGroup: (group) =>
    set((state) => {
      const normalized = normalizeContact(group)
      if (!normalized) return state
      if (state.groups.some((g) => g.id === normalized.id)) {
        return { groups: state.groups.map((g) => (g.id === normalized.id ? { ...g, ...normalized } : g)) }
      }
      return { groups: [...state.groups, normalized] }
    }),
  updatePresence: (id, status) =>
    set((state) => ({
      contacts: state.contacts.map((c) => (c.id === String(id) ? { ...c, status: normalizeStatus(status) } : c)),
      groups: state.groups.map((g) => (g.id === String(id) ? { ...g, status: normalizeStatus(status) } : g))
    })),
  setSearchQuery: (q) => set({ searchQuery: q }),
  setSelectedContactId: (id) => set({ selectedContactId: id === null ? null : String(id) })
}))
