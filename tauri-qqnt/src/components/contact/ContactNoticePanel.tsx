import { Bell, Filter, Trash2 } from 'lucide-react'

interface ContactNoticePanelProps {
  type: 'friend' | 'group'
}

export function ContactNoticePanel({ type }: ContactNoticePanelProps) {
  const title = type === 'friend' ? '好友通知' : '群通知'

  async function openFilteredNoticeWindow() {
    const url = `index.html#/notice-filter?type=${type}`
    try {
      const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
      new WebviewWindow(`notice-filter-${type}-${Date.now()}`, {
        url,
        title: '已过滤的通知',
        width: 420,
        height: 520,
        minWidth: 360,
        minHeight: 420,
        center: true,
        resizable: true,
        decorations: true,
        visible: true
      })
    } catch {
      window.open(`#/notice-filter?type=${type}`, '_blank', 'width=420,height=520')
    }
  }

  return (
    <section className="flex h-full min-h-0 flex-col bg-[var(--qq-bg)]">
      <header className="flex h-12 items-center justify-between border-b border-[var(--qq-border)] px-4">
        <h3 className="text-base font-semibold text-[var(--qq-text)]">{title}</h3>
        <div className="flex items-center gap-2 text-[var(--qq-text-secondary)]">
          <button
            onClick={() => void openFilteredNoticeWindow()}
            className="rounded p-1.5 hover:bg-[var(--qq-bg-secondary)] hover:text-[var(--qq-text)]"
            aria-label="筛选通知"
            title="筛选通知"
          >
            <Filter size={17} />
          </button>
          <button
            className="rounded p-1.5 hover:bg-[var(--qq-bg-secondary)] hover:text-[var(--qq-text)]"
            aria-label="清空通知"
            title="清空通知"
          >
            <Trash2 size={17} />
          </button>
        </div>
      </header>

      <div className="flex flex-1 flex-col items-center justify-center text-[var(--qq-text)]">
        <div className="flex h-24 w-24 items-center justify-center rounded-full border-2 border-current">
          <Bell size={48} strokeWidth={1.8} />
        </div>
        <p className="mt-7 text-base">暂无通知</p>
      </div>
    </section>
  )
}
