import { useState } from 'react'
import { cn } from '@/lib/utils'
import { useUIStore } from '@/stores/uiStore'
import { useAuthStore } from '@/stores/authStore'
import type { ThemeMode } from '@/types/qqnt'

const TABS = [
  { key: 'general', label: '通用' },
  { key: 'account', label: '账号' },
  { key: 'notification', label: '通知' },
  { key: 'file', label: '文件' },
  { key: 'network', label: '网络' },
  { key: 'e2e', label: 'E2E' },
  { key: 'about', label: '关于' }
] as const

type TabKey = (typeof TABS)[number]['key']

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between border-b border-[var(--qq-border)] py-3 last:border-0">
      <span className="text-sm text-[var(--qq-text)]">{label}</span>
      <div className="flex items-center gap-2">{children}</div>
    </div>
  )
}

function ThemeSelector() {
  const theme = useUIStore((state) => state.theme)
  const setTheme = useUIStore((state) => state.setTheme)
  const modes: { value: ThemeMode; label: string }[] = [
    { value: 'light', label: '浅色' },
    { value: 'dark', label: '深色' },
    { value: 'system', label: '跟随系统' }
  ]
  return (
    <div className="flex rounded-lg bg-[var(--qq-bg-tertiary)] p-1">
      {modes.map((m) => (
        <button
          key={m.value}
          onClick={() => setTheme(m.value)}
          className={cn(
            'rounded-md px-3 py-1 text-xs font-medium transition-colors',
            theme === m.value
              ? 'bg-[var(--qq-surface)] text-[var(--qq-text)] shadow-sm'
              : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
          )}
        >
          {m.label}
        </button>
      ))}
    </div>
  )
}

export function SettingsView() {
  const [active, setActive] = useState<TabKey>('general')
  const currentUser = useAuthStore((state) => state.currentUser)

  const panels: Record<TabKey, React.ReactNode> = {
    general: (
      <div>
        <Row label="主题">
          <ThemeSelector />
        </Row>
        <Row label="开机自启">
          <input type="checkbox" disabled className="accent-[var(--qq-primary)]" />
        </Row>
        <Row label="最小化到托盘">
          <input type="checkbox" defaultChecked className="accent-[var(--qq-primary)]" />
        </Row>
      </div>
    ),
    account: (
      <div>
        <Row label="当前账号">{currentUser?.nickname || '未登录'}</Row>
        <Row label="QQ 号">{currentUser?.id || '-'}</Row>
        <Row label="修改密码">
          <button className="rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-border)]">
            修改
          </button>
        </Row>
      </div>
    ),
    notification: (
      <div>
        <Row label="消息通知">
          <input type="checkbox" defaultChecked className="accent-[var(--qq-primary)]" />
        </Row>
        <Row label="声音">
          <input type="checkbox" defaultChecked className="accent-[var(--qq-primary)]" />
        </Row>
        <Row label="桌面通知">
          <input type="checkbox" defaultChecked className="accent-[var(--qq-primary)]" />
        </Row>
      </div>
    ),
    file: (
      <div>
        <Row label="默认下载目录">
          <button className="rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-border)]">
            选择文件夹
          </button>
        </Row>
        <Row label="自动接收文件（不超过 100 MB）">
          <input type="checkbox" className="accent-[var(--qq-primary)]" />
        </Row>
      </div>
    ),
    network: (
      <div>
        <Row label="服务器地址">127.0.0.1</Row>
        <Row label="端口">16000</Row>
        <Row label="使用代理">
          <input type="checkbox" className="accent-[var(--qq-primary)]" />
        </Row>
      </div>
    ),
    e2e: (
      <div>
        <Row label="端到端加密状态">
          <span className="text-xs text-[var(--qq-success)]">已启用</span>
        </Row>
        <Row label="安全号码">
          <button className="rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-border)]">
            查看
          </button>
        </Row>
        <Row label="重置身份密钥">
          <button className="rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-danger)] hover:bg-[var(--qq-border)]">
            重置
          </button>
        </Row>
      </div>
    ),
    about: (
      <div>
        <Row label="QQ NT">v0.1.0</Row>
        <Row label="Tauri">v2</Row>
        <Row label="协议版本">v1</Row>
      </div>
    )
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <aside className="w-40 border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] py-2">
        {TABS.map((tab) => (
          <button
            key={tab.key}
            onClick={() => setActive(tab.key)}
            className={cn(
              'w-full px-4 py-2 text-left text-sm transition-colors',
              active === tab.key
                ? 'bg-[var(--qq-primary-soft)] font-medium text-[var(--qq-primary)]'
                : 'text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]'
            )}
          >
            {tab.label}
          </button>
        ))}
      </aside>
      <main className="flex-1 p-6">
        <h2 className="mb-4 text-base font-semibold text-[var(--qq-text)]">
          {TABS.find((t) => t.key === active)?.label}
        </h2>
        <div className="max-w-xl rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-4">
          {panels[active]}
        </div>
      </main>
    </div>
  )
}
