import { create } from 'zustand'
import type { FileInfo } from '@/types/qqnt'

interface FileState {
  transfers: Record<string, FileInfo>
  setProgress: (id: string, progress: number, patch?: Partial<FileInfo>) => void
  addTransfer: (file: FileInfo) => void
  completeTransfer: (id: string, patch?: Partial<FileInfo>) => void
  failTransfer: (id: string, error: string) => void
  removeTransfer: (id: string) => void
}

const EMPTY_TRANSFER: FileInfo = {
  id: '',
  name: '未知文件',
  size: 0,
  mime: 'application/octet-stream',
  progress: 0
}

export const useFileStore = create<FileState>((set) => ({
  transfers: {},
  setProgress: (id, progress, patch = {}) =>
    set((state) => ({
      transfers: {
        ...state.transfers,
        [id]: {
          ...EMPTY_TRANSFER,
          ...state.transfers[id],
          ...patch,
          id,
          progress: Math.max(0, Math.min(100, progress))
        }
      }
    })),
  addTransfer: (file) =>
    set((state) => ({
      transfers: { ...state.transfers, [file.id]: { ...state.transfers[file.id], ...file } }
    })),
  completeTransfer: (id, patch = {}) =>
    set((state) => ({
      transfers: {
        ...state.transfers,
        [id]: { ...EMPTY_TRANSFER, ...state.transfers[id], ...patch, id, progress: 100, error: undefined }
      }
    })),
  failTransfer: (id, error) =>
    set((state) => ({
      transfers: {
        ...state.transfers,
        [id]: { ...EMPTY_TRANSFER, ...state.transfers[id], id, error }
      }
    })),
  removeTransfer: (id) =>
    set((state) => {
      const { [id]: removedTransfer, ...rest } = state.transfers
      void removedTransfer
      return { transfers: rest }
    })
}))
