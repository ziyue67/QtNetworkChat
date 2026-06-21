import { useSearchParams } from 'react-router-dom'
import { Bell } from 'lucide-react'

export function NoticeFilterWindow() {
  const [searchParams] = useSearchParams()
  const type = searchParams.get('type') === 'group' ? '群通知' : '好友通知'

  return (
    <main className="flex h-full w-full flex-col bg-[var(--qq-bg)] text-[var(--qq-text)]">
      <header className="flex h-12 items-center border-b border-[var(--qq-border)] px-4">
        <h1 className="text-sm font-medium text-[var(--qq-text)]">已过滤的通知</h1>
      </header>

      <section className="flex flex-1 flex-col items-center justify-center">
        <div className="flex h-24 w-24 items-center justify-center rounded-full border-2 border-[var(--qq-text-secondary)]/40 text-[var(--qq-text-secondary)]">
          <Bell size={48} strokeWidth={1.5} />
        </div>
        <p className="mt-6 text-sm text-[var(--qq-text)]">暂无通知</p>
        <p className="mt-1 text-xs text-[var(--qq-text-tertiary)]">{type} 中没有符合筛选条件的通知</p>
      </section>
    </main>
  )
}
