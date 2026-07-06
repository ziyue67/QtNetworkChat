import { Settings, Moon, Sun, Monitor, Bell, FolderOpen, Shield, Info, Keyboard } from 'lucide-react'
import { useEffect, useState } from 'react'
import { open } from '@tauri-apps/plugin-dialog'
import { cn } from '@/lib/utils'
import { useUIStore, type QQNTSettings } from '@/stores/uiStore'
import { useAuthStore } from '@/stores/authStore'
import { settingsSync } from '@/api/qqnt'
import type { ThemeMode } from '@/types/qqnt'
import { DEFAULT_SCREENSHOT_SHORTCUT, formatShortcutLabel, shortcutFromKeyboardEvent } from '@/lib/shortcut'

const TABS = [
  { key: 'general', label: '通用', icon: Settings },
  { key: 'account', label: '账号', icon: Info },
  { key: 'notification', label: '通知', icon: Bell },
  { key: 'shortcut', label: '快捷键', icon: Keyboard },
  { key: 'file', label: '文件', icon: FolderOpen },
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

function ShortcutRecorder({
  value,
  onChange
}: {
  value: string
  onChange: (shortcut: string) => void
}) {
  const [recording, setRecording] = useState(false)
  const [error, setError] = useState('')

  function handleKeyDown(event: React.KeyboardEvent<HTMLButtonElement>) {
    if (!recording) return
    event.preventDefault()
    event.stopPropagation()

    if (event.key === 'Escape') {
      setRecording(false)
      setError('')
      return
    }

    const nextShortcut = shortcutFromKeyboardEvent(event)
    if (!nextShortcut) {
      setError('请按下一个完整组合键')
      return
    }

    onChange(nextShortcut)
    setRecording(false)
    setError('')
  }

  return (
    <div className="flex flex-col items-end gap-1.5">
      <div className="flex items-center gap-2">
        <button
          type="button"
          onClick={() => {
            setRecording(true)
            setError('')
          }}
          onKeyDown={handleKeyDown}
          className={cn(
            'min-w-40 rounded-lg border px-3 py-1.5 text-center text-xs font-medium outline-none transition-colors',
            recording
              ? 'border-[var(--qq-primary)] bg-[var(--qq-primary-soft)] text-[var(--qq-primary)] ring-2 ring-[var(--qq-primary)]/15'
              : 'border-[var(--qq-border)] bg-[var(--qq-bg-tertiary)] text-[var(--qq-text)] hover:border-[var(--qq-primary)]/40'
          )}
        >
          {recording ? '请按新的组合键' : formatShortcutLabel(value)}
        </button>
        <button
          type="button"
          onClick={() => {
            onChange(DEFAULT_SCREENSHOT_SHORTCUT)
            setRecording(false)
            setError('')
          }}
          className="rounded-md px-2.5 py-1.5 text-xs text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
        >
          恢复默认
        </button>
      </div>
      <span className={cn('text-[11px]', error ? 'text-[var(--qq-danger)]' : 'text-[var(--qq-text-tertiary)]')}>
        {error || (recording ? '按 Esc 取消，建议使用 Ctrl/Alt/Shift + 字母' : '点击快捷键框后直接按新的组合键')}
      </span>
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
  const [syncText, setSyncText] = useState('已保存到本地')
  const currentUser = useAuthStore((state) => state.currentUser)
  const settings = useUIStore((state) => state.settings)
  const setTheme = useUIStore((state) => state.setTheme)
  const updateSettings = useUIStore((state) => state.updateSettings)
  const ensureDefaultDownloadPath = useUIStore((state) => state.ensureDefaultDownloadPath)

  useEffect(() => {
    void ensureDefaultDownloadPath()
  }, [ensureDefaultDownloadPath])

  useEffect(() => {
    const timer = window.setTimeout(() => {
      settingsSync({ ...settings })
        .then((ack) => setSyncText(ack.status === 'ok' ? '已同步到引擎' : ack.error?.message || '同步失败'))
        .catch(() => setSyncText('已保存到本地，等待引擎连接'))
    }, 350)
    return () => window.clearTimeout(timer)
  }, [settings])

  function patchSettings(patch: Partial<QQNTSettings>) {
    updateSettings(patch)
    setSyncText('正在同步...')
  }

  async function chooseDownloadDirectory() {
    try {
      const selected = await open({
        directory: true,
        multiple: false,
        title: '选择默认下载目录'
      })
      if (typeof selected === 'string') patchSettings({ downloadPath: selected })
    } catch {
      setSyncText('无法打开系统资源管理器')
    }
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
    shortcut: (
      <div>
        <Row label="截图快捷键">
          <ShortcutRecorder value={settings.screenshotShortcut} onChange={(screenshotShortcut) => patchSettings({ screenshotShortcut })} />
        </Row>
        <Row label="截图时隐藏当前窗口">
          <Toggle
            checked={settings.hideWindowBeforeScreenshot}
            onChange={(hideWindowBeforeScreenshot) => patchSettings({ hideWindowBeforeScreenshot })}
          />
        </Row>
        <Row label="截图入口">
          <span className="text-xs text-[var(--qq-text-secondary)]">聊天输入框工具栏 · 剪刀按钮</span>
        </Row>
        <Row label="生效方式">
          <span className="text-xs text-[var(--qq-text-secondary)]">保存后自动重新注册全局快捷键</span>
        </Row>
      </div>
    ),
    file: (
      <div>
        <Row label="默认下载目录">
          <button
            onClick={() => void chooseDownloadDirectory()}
            className="max-w-56 truncate rounded-md bg-[var(--qq-bg-tertiary)] px-3 py-1.5 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-border)]"
            title={settings.downloadPath || '选择文件夹'}
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
