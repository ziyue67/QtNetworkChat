import { create } from 'zustand'
import type { Contact } from '@/types/qqnt'

const VALID_STATUSES = new Set<Contact['status']>(['online', 'offline', 'busy', 'away'])

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

  return {
    ...contact,
    id,
    nickname: text(raw.nickname, id),
    avatar: text(raw.avatar) || undefined,
    status: normalizeStatus(raw.status),
    signature: text(raw.signature) || undefined,
    remark: text(raw.remark) || undefined,
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

interface ContactState {
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
