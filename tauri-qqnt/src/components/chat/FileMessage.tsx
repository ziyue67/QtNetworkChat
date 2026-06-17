import { FileText, Download, FolderOpen, X, FileImage } from 'lucide-react'
import { useFileStore } from '@/stores/fileStore'
import { cn } from '@/lib/utils'
import type { Message } from '@/types/qqnt'

function formatSize(bytes: number) {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

interface FileMessageProps {
  message: Message
  isSelf: boolean
  onCancel?: (id: string) => void
  onDownload?: (message: Message) => void
  onOpenFolder?: (message: Message) => void
}

export function FileMessage({ message, isSelf, onCancel, onDownload, onOpenFolder }: FileMessageProps) {
  const fileInfo = message.fileInfo
  const transfers = useFileStore((state) => state.transfers)
  const transfer = fileInfo ? transfers[fileInfo.id] : undefined
  const progress = transfer?.progress ?? fileInfo?.progress ?? 0
  const isImage = fileInfo?.mime?.startsWith('image/') || message.type === 'image'
  const isDone = progress >= 100
  const isUploading = isSelf && !isDone
  const fileName = fileInfo?.name || message.content || '未知文件'

  return (
    <div className={cn('flex items-start gap-3', isSelf ? 'flex-row-reverse' : 'flex-row')}>
      <div
        className={cn(
          'max-w-[70%] min-w-[180px] rounded-lg p-3',
          isSelf ? 'bg-[var(--qq-primary)] text-white' : 'bg-[var(--qq-bg-tertiary)] text-[var(--qq-text)]'
        )}
      >
        <div className="flex items-start gap-3">
          <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-white/20 text-white/90">
            {isImage ? <FileImage size={20} /> : <FileText size={20} />}
          </div>
          <div className="min-w-0 flex-1">
            <p className="truncate text-sm font-medium">{fileName}</p>
            {fileInfo?.size ? (
              <p className="mt-0.5 text-[10px] opacity-80">{formatSize(fileInfo.size)}</p>
            ) : null}
          </div>
        </div>

        {progress < 100 ? (
          <div className="mt-2">
            <div className="h-1.5 w-full overflow-hidden rounded-full bg-black/10">
              <div
                className="h-full rounded-full bg-white/90 transition-all"
                style={{ width: `${progress}%` }}
              />
            </div>
            <div className="mt-1 flex items-center justify-between text-[10px] opacity-80">
              <span>{Math.round(progress)}%</span>
              {isUploading ? (
                <button
                  onClick={() => fileInfo && onCancel?.(fileInfo.id)}
                  className="rounded p-0.5 hover:bg-white/20"
                >
                  <X size={10} />
                </button>
              ) : null}
            </div>
          </div>
        ) : (
          <div className="mt-2 flex items-center gap-2">
            <button
              onClick={() => onDownload?.(message)}
              className="flex items-center gap-1 rounded bg-white/20 px-2 py-1 text-[10px] font-medium hover:bg-white/30"
            >
              <Download size={10} />
              下载
            </button>
            <button
              onClick={() => onOpenFolder?.(message)}
              className="flex items-center gap-1 rounded bg-white/20 px-2 py-1 text-[10px] font-medium hover:bg-white/30"
            >
              <FolderOpen size={10} />
              文件夹
            </button>
          </div>
        )}
      </div>
    </div>
  )
}
