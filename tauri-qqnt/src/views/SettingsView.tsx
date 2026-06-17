import { Settings, Moon, Sun, Monitor, Bell, FolderOpen, Shield, Info, Network } from 'lucide-react'
import { useEffect, useState } from 'react'
import { cn } from '@/lib/utils'
import { useUIStore, type QQNTSettings } from '@/stores/uiStore'
import { useAuthStore } from '@/stores/authStore'
import { settingsSync } from '@/api/qqnt'
import type { ThemeMode } from '@/types/qqnt'

const TABS = [
  { key: 'general', label: '通用', icon: Settings },
  { key: 'account', label: '账号', icon: Info },
  { key: 'notification', label: '通知', icon: Bell },
  { key: 'file', label: '文件', icon: FolderOpen },
  { key: 'network', label: '网络', icon: Network },
  { key: 'e2e', label: 'E2E', icon: Shield },
  { key: 'about', label: '关于', icon: Info }
] as const

type TabKey = (typeof TABS)[number]['key']

function ThemeSelector({ onChange }: { onChange: (theme: ThemeMode) => void }) {
  const theme = useUIStore((state) => state.theme)
  const modes: { value: ThemeMode; label: string; icon: typeof Sun }[] = [
    { value: 'light', label: '浅色', icon: Sun },
    { value: 'dark', label: '深色', icon: Moon },
    { value: 'system', label: '跟随系统', icon: Monitor }
  ]
  return (
    <div className="flex rounded-lg bg-[var(--qq-bg-tertiary)] p-1">
      {modes.map((mode) => (
        <button
          key={mode.value}
          onClick={() => onChange(mode.value)}
          className={cn(
            'flex items-center gap-1.5 rounded-md px-3 py-1.5 text-xs font-medium transition-colors',
            theme === mode.value
              ? 'bg-[var(--qq-surface)] text-[var(--qq-text)] shadow-sm'
              : 'text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
          )}
        >
          <mode.icon size={12} />
          {mode.label}
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

function Toggle({ checked, onChange }: { checked: boolean; onChange: (checked: boolean) => void }) {
  return (
    <input
      type="checkbox"
      checked={checked}
      onChange={(event) => onChange(event.currentTarget.checked)}
      className="accent-[var(--qq-primary)]"
    />
  )
}

function SmallInput({ value, onChange, type = 'text' }: { value: string; type?: string; onChange: (value: string) => void }) {
  return (
    <input
      value={value}
      type={type}
      onChange={(event) => onChange(event.currentTarget.value)}
      className="w-44 rounded-md border border-[var(--qq-border)] bg-[var(--qq-bg)] px-2 py-1.5 text-xs text-[var(--qq-text)] outline-none focus:border-[var(--qq-primary)]"
    />
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
  const [syncText, setSyncText] = useState('已保存到本地')
  const currentUser = useAuthStore((state) => state.currentUser)
  const setServer = useAuthStore((state) => state.setServer)
  const settings = useUIStore((state) => state.settings)
  const setTheme = useUIStore((state) => state.setTheme)
  const updateSettings = useUIStore((state) => state.updateSettings)

  useEffect(() => {
    setServer(settings.networkHost, settings.networkPort)
    const timer = window.setTimeout(() => {
      settingsSync({ ...settings })
        .then((ack) => setSyncText(ack.status === 'ok' ? '已同步到引擎' : ack.error?.message || '同步失败'))
        .catch(() => setSyncText('已保存到本地，等待引擎连接'))
    }, 350)
    return () => window.clearTimeout(timer)
  }, [setServer, settings])

  function patchSettings(patch: Partial<QQNTSettings>) {
    updateSettings(patch)
    setSyncText('正在同步...')
  }

  const panels: Record<TabKey, React.ReactNode> = {
    general: (
      <div>
        <Row label="主题">
          <ThemeSelector
            onChange={(theme) => {
              setTheme(theme)
              setSyncText('正在同步...')
            }}
          />
        </Row>
        <Row label="开机自启">
          <Toggle checked={settings.launchOnStartup} onChange={(launchOnStartup) => patchSettings({ launchOnStartup })} />
        </Row>
        <Row label="最小化到托盘">
          <Toggle checked={settings.minimizeToTray} onChange={(minimizeToTray) => patchSettings({ minimizeToTray })} />
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
        <Row label="资料同步">
          <span className="text-xs text-[var(--qq-text-secondary)]">个人资料页保存后同步</span>
        </Row>
      </div>
    ),
    notification: (
      <div>
        <Row label="消息通知">
          <Toggle checked={settings.notifications} onChange={(notifications) => patchSettings({ notifications })} />
        </Row>
        <Row label="声音">
          <Toggle checked={settings.sound} onChange={(sound) => patchSettings({ sound })} />
        </Row>
        <Row label="桌面通知">
          <Toggle checked={settings.desktopNotifications} onChange={(desktopNotifications) => patchSettings({ desktopNotifications })} />
        </Row>
        <Row label="会话内消息免打扰">
          <Toggle checked={settings.muteInSession} onChange={(muteInSession) => patchSettings({ muteInSession })} />
        </Row>
      </div>
    ),
    file: (
      <div>
        <Row label="默认下载目录">
          <button
            onClick={() => {
              const next = window.prompt('默认下载目录', settings.downloadPath)
              if (next !== null) patchSettings({ downloadPath: next })
            }}
            className="max-w-56 truncate rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-border)]"
          >
            {settings.downloadPath || '选择文件夹'}
          </button>
        </Row>
        <Row label="自动接收文件（不超过 100 MB）">
          <Toggle checked={settings.autoAcceptFiles} onChange={(autoAcceptFiles) => patchSettings({ autoAcceptFiles })} />
        </Row>
        <Row label="下载完成后打开文件夹">
          <Toggle
            checked={settings.openFolderAfterDownload}
            onChange={(openFolderAfterDownload) => patchSettings({ openFolderAfterDownload })}
          />
        </Row>
      </div>
    ),
    network: (
      <div>
        <Row label="引擎地址">
          <SmallInput value={settings.networkHost} onChange={(networkHost) => patchSettings({ networkHost })} />
        </Row>
        <Row label="引擎端口">
          <SmallInput
            value={String(settings.networkPort)}
            type="number"
            onChange={(value) => patchSettings({ networkPort: Number(value) || 16000 })}
          />
        </Row>
        <Row label="说明">
          <span className="max-w-72 text-right text-xs text-[var(--qq-text-secondary)]">
            登录页不显示服务器配置；这里仅供登录后联调本地客户端引擎。
          </span>
        </Row>
      </div>
    ),
    e2e: (
      <div>
        <Row label="端到端加密">
          <Toggle checked={settings.e2eEnabled} onChange={(e2eEnabled) => patchSettings({ e2eEnabled })} />
        </Row>
        <Row label="安全号码">
          <button className="rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-border)]">
            查看
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
        <Row label="设置同步">{syncText}</Row>
      </div>
    )
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <SettingsSidebar active={active} onChange={setActive} />
      <main className="flex-1 p-6">
        <div className="mb-4 flex items-center justify-between">
          <h2 className="text-base font-semibold text-[var(--qq-text)]">{TABS.find((tab) => tab.key === active)?.label}</h2>
          <span className="text-xs text-[var(--qq-text-secondary)]">{syncText}</span>
        </div>
        <div className="max-w-2xl rounded-xl border border-[var(--qq-border)] bg-[var(--qq-surface)] p-4">
          {panels[active]}
        </div>
      </main>
    </div>
  )
}
