import { create } from 'zustand'
import type { FileInfo } from '@/types/qqnt'

interface FileState {
  transfers: Record<string, FileInfo>
  setProgress: (id: string, progress: number) => void
  addTransfer: (file: FileInfo) => void
  removeTransfer: (id: string) => void
}

export const useFileStore = create<FileState>((set) => ({
  transfers: {},
  setProgress: (id, progress) =>
    set((state) => ({
      transfers: {
        ...state.transfers,
        [id]: { ...(state.transfers[id] || ({} as FileInfo)), progress }
      }
    })),
  addTransfer: (file) =>
    set((state) => ({
      transfers: { ...state.transfers, [file.id]: file }
    })),
  removeTransfer: (id) =>
    set((state) => {
      const { [id]: _, ...rest } = state.transfers
      return { transfers: rest }
    })
}))