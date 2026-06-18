import { Search, X } from 'lucide-react'
import { cn } from '@/lib/utils'

interface SearchBarProps {
  value: string
  onChange: (value: string) => void
  placeholder?: string
  className?: string
}

export function SearchBar({ value, onChange, placeholder = '搜索', className }: SearchBarProps) {
  return (
    <div
      className={cn(
        'flex items-center gap-2 rounded-lg bg-[var(--qq-bg)] px-3 py-2 transition-colors focus-within:bg-[var(--qq-bg-secondary)]',
        className
      )}
    >
      <Search size={14} className="text-[var(--qq-text-tertiary)]" />
      <input
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder={placeholder}
        className="flex-1 bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
      />
      {value ? (
        <button
          onClick={() => onChange('')}
          className="rounded p-0.5 text-[var(--qq-text-tertiary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
          aria-label="清空"
        >
          <X size={12} />
        </button>
      ) : null}
    </div>
  )
}
