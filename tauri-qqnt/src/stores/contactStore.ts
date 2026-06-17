import { create } from 'zustand'
import type { Contact } from '@/types/qqnt'

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
  setContacts: (contacts) => set({ contacts }),
  setGroups: (groups) => set({ groups }),
  addContact: (contact) =>
    set((state) => {
      if (state.contacts.some((c) => c.id === contact.id)) return state
      return { contacts: [...state.contacts, contact] }
    }),
  addGroup: (group) =>
    set((state) => {
      if (state.groups.some((g) => g.id === group.id)) return state
      return { groups: [...state.groups, group] }
    }),
  updatePresence: (id, status) =>
    set((state) => ({
      contacts: state.contacts.map((c) => (c.id === id ? { ...c, status } : c)),
      groups: state.groups.map((g) => (g.id === id ? { ...g, status } : g))
    })),
  setSearchQuery: (q) => set({ searchQuery: q }),
  setSelectedContactId: (id) => set({ selectedContactId: id })
}))
