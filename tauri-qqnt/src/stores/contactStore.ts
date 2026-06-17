import { create } from 'zustand'
import type { Contact } from '@/types/qqnt'

interface ContactState {
  contacts: Contact[]
  groups: Contact[]
  searchQuery: string
  setContacts: (contacts: Contact[]) => void
  updatePresence: (id: string, status: Contact['status']) => void
  setSearchQuery: (q: string) => void
}

export const useContactStore = create<ContactState>((set) => ({
  contacts: [],
  groups: [],
  searchQuery: '',
  setContacts: (contacts) => set({ contacts }),
  updatePresence: (id, status) =>
    set((state) => ({
      contacts: state.contacts.map((c) => (c.id === id ? { ...c, status } : c))
    })),
  setSearchQuery: (q) => set({ searchQuery: q })
}))