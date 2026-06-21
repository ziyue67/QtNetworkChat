import { useMemo, useState } from 'react'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { Check, Search, Smile, UserRound, UsersRound, X } from 'lucide-react'
import { forwardLocalMessage } from '@/api/qqnt'
import { Avatar } from '@/components/common/Avatar'
import { getNormalizedGroup, useContactStore } from '@/stores/contactStore'
import { normalizeSession, normalizeSessionId, useSessionStore } from '@/stores/sessionStore'
import { cn } from '@/lib/utils'
import type { Contact, Message, Session } from '@/types/qqnt'

const SNAPSHOT_KEY = 'qqnt:forward-snapshot'
const CATEGORIES = ['特别关心', '我的好友', '朋友', '家人', '同学', '公会的人', '群聊']

interface ForwardSnapshot {
  message?: Message
  contacts?: Contact[]
  groups?: Contact[]
  sessions?: Session[]
}

interface ForwardTarget {
  id: string
  name: string
  avatar?: string
  subtitle?: string
  category: string
  type: 'private' | 'group'
}

function readSnapshot(): ForwardSnapshot {
  try {
    return JSON.parse(window.localStorage.getItem(SNAPSHOT_KEY) || '{}') as ForwardSnapshot
  } catch {
    return {}
  }
}

function previewText(message?: Message) {
  if (!message) return '已选择 1 条消息'
  if (message.type === 'image') return '[图片]'
  if (message.type === 'file') return `[文件] ${message.fileInfo?.name || message.content}`
  return message.content || '文本消息'
}

function uniqueTargets(targets: ForwardTarget[]) {
  const seen = new Set<string>()
  return targets.filter((target) => {
    const key = `${target.type}:${normalizeSessionId(target.id)}`
    if (seen.has(key)) return false
    seen.add(key)
    return true
  })
}

function buildTargets(
  contacts: Contact[],
  groups: Contact[],
  sessions: Session[]
): ForwardTarget[] {
  const friendTargets = contacts.map((contact) => ({
    id: normalizeSessionId(contact.id),
    name: contact.remark || contact.nickname || contact.id,
    avatar: contact.avatar,
    subtitle: contact.nickname === contact.remark ? contact.id : contact.nickname || contact.id,
    category: contact.tags?.includes('特别关心') || contact.category === '特别关心' ? '特别关心' : getNormalizedGroup(contact),
    type: 'private' as const
  }))
  const groupTargets = [
    ...groups.map((group) => ({
      id: normalizeSessionId(group.id),
      name: group.nickname || group.id,
      avatar: group.avatar,
      subtitle: group.memberCount ? `${group.memberCount} 人` : '群聊',
      category: '群聊',
      type: 'group' as const
    })),
    ...sessions
      .filter((session) => session.type === 'group')
      .map((session) => ({
        id: normalizeSessionId(session.id),
        name: session.name || session.id,
        avatar: session.avatar,
        subtitle: session.lastMessage || '群聊',
        category: '群聊',
        type: 'group' as const
      }))
  ]
  return uniqueTargets([...friendTargets, ...groupTargets])
}

export function ForwardWindow() {
  const snapshot = useMemo(readSnapshot, [])
  const storeContacts = useContactStore((state) => state.contacts)
  const storeGroups = useContactStore((state) => state.groups)
  const storeSessions = useSessionStore((state) => state.sessions)
  const contacts = snapshot.contacts?.length ? snapshot.contacts : storeContacts
  const groups = snapshot.groups?.length ? snapshot.groups : storeGroups
  const sessions = (snapshot.sessions?.length ? snapshot.sessions : storeSessions).map(normalizeSession)
  const targets = useMemo(() => buildTargets(contacts, groups, sessions), [contacts, groups, sessions])
  const firstCategoryWithData = useMemo(
    () => CATEGORIES.find((category) => targets.some((target) => target.category === category)) || '特别关心',
    [targets]
  )
  const [activeCategory, setActiveCategory] = useState(firstCategoryWithData)
  const [query, setQuery] = useState('')
  const [note, setNote] = useState('')
  const [selectedTarget, setSelectedTarget] = useState<ForwardTarget | null>(null)
  const [status, setStatus] = useState('')

  const close = () => {
    void getCurrentWindow().close().catch(() => undefined)
  }

  const filteredTargets = targets.filter((target) => {
    const byCategory = target.category === activeCategory
    const normalizedQuery = query.trim().toLowerCase()
    const byQuery = !normalizedQuery ||
      target.name.toLowerCase().includes(normalizedQuery) ||
      target.id.toLowerCase().includes(normalizedQuery) ||
      target.subtitle?.toLowerCase().includes(normalizedQuery)
    return byCategory && byQuery
  })

  const categoryCount = (category: string) => targets.filter((target) => target.category === category).length

  const submit = async () => {
    if (!selectedTarget) return
    try {
      await forwardLocalMessage(snapshot.message || null, selectedTarget, note)
      setStatus('已保存转发记录。')
      window.setTimeout(close, 350)
    } catch (error) {
      setStatus(error instanceof Error ? `转发失败：${error.message}` : '转发失败')
    }
  }

  return (
    <div className="flex h-full w-full flex-col overflow-hidden bg-[#f6f7fb] text-[#111827]">
      <div
        data-tauri-drag-region
        className="flex h-10 shrink-0 items-center justify-between border-b border-[#e6e8ef] bg-white px-3"
      >
        <div data-tauri-drag-region className="text-sm font-medium">转发</div>
        <button
          type="button"
          aria-label="关闭"
          onClick={close}
          className="flex h-7 w-7 items-center justify-center rounded-md text-[#6b7280] hover:bg-[#eef1f6] hover:text-[#111827]"
        >
          <X size={16} />
        </button>
      </div>

      <div className="flex min-h-0 flex-1">
        <aside className="flex w-[216px] shrink-0 flex-col border-r border-[#e6e8ef] bg-white">
          <div className="border-b border-[#eef0f4] p-3">
            <label className="flex h-8 items-center gap-2 rounded-md bg-[#f2f4f8] px-2 text-xs text-[#6b7280]">
              <Search size={15} />
              <input
                value={query}
                onChange={(event) => setQuery(event.target.value)}
                className="min-w-0 flex-1 bg-transparent outline-none placeholder:text-[#a0a7b4]"
                placeholder="搜索"
                autoFocus
              />
            </label>
            <button
              type="button"
              onClick={() => setStatus('未开放之后添加。')}
              className="mt-3 flex h-9 w-full items-center justify-center gap-2 rounded-md border border-[#d9e7ff] bg-[#f3f8ff] text-sm text-[#1677ff] hover:bg-[#e8f2ff]"
            >
              <UsersRound size={16} />
              创建群聊并转发
            </button>
          </div>
          <div className="flex-1 overflow-auto py-2">
            {CATEGORIES.map((category) => (
              <button
                key={category}
                type="button"
                onClick={() => {
                  setActiveCategory(category)
                  setSelectedTarget(null)
                }}
                className={cn(
                  'flex h-10 w-full items-center justify-between px-4 text-left text-sm transition-colors',
                  activeCategory === category
                    ? 'bg-[#eaf3ff] text-[#1677ff]'
                    : 'text-[#303642] hover:bg-[#f5f7fb]'
                )}
              >
                <span>{category}</span>
                {categoryCount(category) ? (
                  <span className="text-xs text-[#9aa3b2]">{categoryCount(category)}</span>
                ) : null}
              </button>
            ))}
          </div>
        </aside>

        <main className="flex min-w-0 flex-1 flex-col bg-[#fafbfe]">
          <div className="border-b border-[#e6e8ef] bg-white px-5 py-4">
            <div className="text-sm font-medium">发送给:</div>
            <div className="mt-2 flex h-9 items-center rounded-md border border-dashed border-[#d7dce6] bg-[#fbfcff] px-3 text-xs text-[#9aa3b2]">
              {selectedTarget ? (
                <span className="inline-flex items-center gap-2 rounded-full bg-[#eaf3ff] px-2 py-1 text-[#1677ff]">
                  {selectedTarget.type === 'group' ? <UsersRound size={13} /> : <UserRound size={13} />}
                  {selectedTarget.name}
                </span>
              ) : (
                '暂未选择联系人'
              )}
            </div>
          </div>

          <div className="flex min-h-0 flex-1 gap-4 overflow-hidden p-5">
            <section className="flex min-w-0 flex-1 flex-col overflow-hidden rounded-lg border border-[#e1e6ef] bg-white shadow-sm">
              <div className="flex h-11 items-center justify-between border-b border-[#eef0f4] px-4">
                <span className="text-sm font-medium">{activeCategory}</span>
                <span className="text-xs text-[#9aa3b2]">{filteredTargets.length} 个可选</span>
              </div>
              <div className="flex-1 overflow-auto py-1">
                {filteredTargets.length > 0 ? (
                  filteredTargets.map((target) => (
                    <button
                      key={`${target.type}:${target.id}`}
                      type="button"
                      onClick={() => setSelectedTarget(target)}
                      className={cn(
                        'flex h-14 w-full items-center gap-3 px-4 text-left hover:bg-[#f5f8ff]',
                        selectedTarget?.id === target.id && selectedTarget.type === target.type && 'bg-[#eef6ff]'
                      )}
                    >
                      <Avatar src={target.avatar} fallback={target.name} size={34} />
                      <div className="min-w-0 flex-1">
                        <div className="truncate text-sm text-[#202733]">{target.name}</div>
                        <div className="truncate text-xs text-[#9aa3b2]">{target.subtitle || target.id}</div>
                      </div>
                      {selectedTarget?.id === target.id && selectedTarget.type === target.type ? (
                        <span className="flex h-5 w-5 items-center justify-center rounded-full bg-[#1677ff] text-white">
                          <Check size={13} />
                        </span>
                      ) : null}
                    </button>
                  ))
                ) : (
                  <div className="flex h-full items-center justify-center text-sm text-[#a0a7b4]">
                    当前分组暂无联系人
                  </div>
                )}
              </div>
            </section>

            <section className="w-[248px] shrink-0 rounded-lg border border-[#e1e6ef] bg-white p-4 shadow-sm">
              <div className="mb-3 text-xs text-[#8a93a3]">转发预览</div>
              <div className="rounded-md bg-[#f4f6fa] px-3 py-3">
                <div className="line-clamp-5 whitespace-pre-wrap break-words text-sm text-[#303642]">
                  {previewText(snapshot.message)}
                </div>
              </div>
              {status ? <div className="mt-3 text-xs text-[#1677ff]">{status}</div> : null}
            </section>
          </div>

          <div className="border-t border-[#e6e8ef] bg-white p-4">
            <label className="flex h-10 items-center gap-2 rounded-md border border-[#dfe4ed] bg-white px-3">
              <input
                value={note}
                onChange={(event) => setNote(event.target.value)}
                className="min-w-0 flex-1 text-sm outline-none placeholder:text-[#a0a7b4]"
                placeholder="留言"
              />
              <Smile size={18} className="text-[#8a93a3]" />
            </label>
            <div className="mt-4 flex justify-end gap-2">
              <button
                type="button"
                onClick={close}
                className="h-8 rounded-md border border-[#d8dde8] px-5 text-sm text-[#303642] hover:bg-[#f5f7fb]"
              >
                取消
              </button>
              <button
                type="button"
                disabled={!selectedTarget}
                onClick={submit}
                className="h-8 rounded-md bg-[#1677ff] px-5 text-sm text-white disabled:opacity-45"
              >
                确定
              </button>
            </div>
          </div>
        </main>
      </div>
    </div>
  )
}
