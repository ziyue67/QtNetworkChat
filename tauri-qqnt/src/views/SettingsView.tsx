import { Settings, Moon, Sun, Monitor, Bell, FolderOpen, Shield, Info } from 'lucide-react'
import { useState } from 'react'
import { cn } from '@/lib/utils'
import { useUIStore } from '@/stores/uiStore'
import { useAuthStore } from '@/stores/authStore'
import type { ThemeMode } from '@/types/qqnt'

const TABS = [
  { key: 'general', label: '通用', icon: Settings },
  { key: 'account', label: '账号', icon: Info },
  { key: 'notification', label: '通知', icon: Bell },
  { key: 'file', label: '文件', icon: FolderOpen },
  { key: 'e2e', label: 'E2E', icon: Shield },
  { key: 'about', label: '关于', icon: Info }
] as const

type TabKey = (typeof TABS)[number]['key']

function ThemeSelector() {
  const theme = useUIStore((state) => state.theme)
  const setTheme = useUIStore((state) => state.setTheme)
  const modes: { value: ThemeMode; label: string; icon: typeof Sun }[] = [
    { value: 'light', label: '浅色', icon: Sun },
    { value: 'dark', label: '深色', icon: Moon },
    { value: 'system', label: '跟随系统', icon: Monitor }
  ]
  return (
    <div className="flex rounded-lg bg-[var(--qq-bg-tertiary)] p-1">
      {modes.map((m) => (
        <button
          key={m.value}
          onClick={() => setTheme(m.value)}
          className={cn(
            'flex items-center gap-1.5 rounded-md px-3 py-1.5 text-xs font-medium transition-colors',
            theme === m.value
              ? 'bg-[var(--qq-surface)] text-[var(--qq-text)] shadow-sm'
              : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
          )}
        >
          <m.icon size={12} />
          {m.label}
        </button>
      ))}
    </div>
  )
}

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between border-b border-[var(--qq-border)] py-3 last:border-0">
      <span className="text-sm text-[var(--qq-text)]">{label}</span>
      <div className="flex items-center gap-2">{children}</div>
    </div>
  )
}

export function SettingsSidebar({ active, onChange }: { active: TabKey; onChange: (key: TabKey) => void }) {
  return (
    <aside className="w-44 border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] py-2">
      {TABS.map((tab) => (
        <button
          key={tab.key}
          onClick={() => onChange(tab.key)}
          className={cn(
            'flex w-full items-center gap-2 px-4 py-2.5 text-left text-sm transition-colors',
            active === tab.key
              ? 'bg-[var(--qq-primary-soft)] font-medium text-[var(--qq-primary)]'
              : 'text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]'
          )}
        >
          <tab.icon size={14} />
          {tab.label}
        </button>
      ))}
    </aside>
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
        <Row label="语言">
          <span className="text-xs text-[var(--qq-text-secondary)]">简体中文</span>
        </Row>
      </div>
    ),
    account: (
      <div>
        <Row label="当前账号">{currentUser?.nickname || '未登录'}</Row>
        <Row label="QQ 号">{currentUser?.id || '-'}</Row>
        <Row label="在线状态">
          <span className="text-xs text-[var(--qq-success)]">在线</span>
        </Row>
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
        <Row label="会话内消息免打扰">
          <input type="checkbox" className="accent-[var(--qq-primary)]" />
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
        <Row label="下载完成后打开文件夹">
          <input type="checkbox" defaultChecked className="accent-[var(--qq-primary)]" />
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
        <Row label="自动轮换周期">
          <span className="text-xs text-[var(--qq-text-secondary)]">90 天</span>
        </Row>
      </div>
    ),
    about: (
      <div>
        <Row label="QQ NT">v0.1.0</Row>
        <Row label="Tauri">v2</Row>
        <Row label="协议版本">v1</Row>
        <Row label="开源协议">MIT</Row>
      </div>
    )
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <SettingsSidebar active={active} onChange={setActive} />
      <main className="flex-1 p-6">
        <h2 className="mb-4 text-base font-semibold text-[var(--qq-text)]">
          {TABS.find((t) => t.key === active)?.label}
        </h2>
        <div className="max-w-2xl rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-4">
          {panels[active]}
        </div>
      </main>
    </div>
  )
}
