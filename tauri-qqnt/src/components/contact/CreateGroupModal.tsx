import { useEffect, useMemo, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import type { LucideIcon } from 'lucide-react'
import {
  BookOpen,
  BriefcaseBusiness,
  Check,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  GraduationCap,
  Gamepad2,
  Home,
  Loader2,
  Package,
  Palette,
  Plus,
  School,
  Search,
  Sparkles,
  Users,
  X
} from 'lucide-react'
import { cn } from '@/lib/utils'
import { Avatar } from '@/components/common/Avatar'
import { useContactStore } from '@/stores/contactStore'
import { useSessionStore } from '@/stores/sessionStore'
import { useUIStore } from '@/stores/uiStore'
import { createGroup } from '@/api/qqnt'
import type { Contact } from '@/types/qqnt'

interface CreateGroupModalProps {
  open: boolean
  onClose: () => void
}

type CreateStep = 'select' | 'category' | 'info'

type GroupCategoryOption = {
  key: string
  label: string
  icon: LucideIcon
  color: string
  children?: string[]
}

type GroupCategorySection = {
  title: string
  options: GroupCategoryOption[]
}

const GROUP_CATEGORY_SECTIONS: GroupCategorySection[] = [
  {
    title: '熟人与家校',
    options: [
      { key: '同学', label: '同学', icon: School, color: 'text-blue-500' },
      { key: '同事', label: '同事', icon: Users, color: 'text-sky-500' },
      { key: '亲友', label: '亲友', icon: Users, color: 'text-cyan-500' },
      { key: '家校', label: '家校', icon: GraduationCap, color: 'text-blue-500' }
    ]
  },
  {
    title: '兴趣娱乐',
    options: [
      { key: '游戏', label: '游戏', icon: Gamepad2, color: 'text-amber-500' },
      { key: '二次元', label: '二次元', icon: Palette, color: 'text-orange-500' },
      {
        key: '更多兴趣',
        label: '更多兴趣',
        icon: Sparkles,
        color: 'text-orange-500',
        children: ['影视', '音乐', '星座', '运动', '读书', '摄影', '舞蹈', '电子产品', '汽车', '美食', '旅游', '交友', '购物', '宠物', '健康', '兼职', '二手闲置', '公益', '其他']
      }
    ]
  },
  {
    title: '学习交流',
    options: [
      {
        key: '行业交流',
        label: '行业交流',
        icon: BriefcaseBusiness,
        color: 'text-emerald-500',
        children: ['投资', 'IT/互联网', '建筑工程', '服务', '传媒', '营销与广告', '教师', '律师', '公务员', '银行', '咨询', '其他']
      },
      {
        key: '学习考试',
        label: '学习考试',
        icon: BookOpen,
        color: 'text-emerald-500',
        children: ['托福', '雅思', 'CET 4/6', 'GRE', 'GMAT', 'MBA', '考研', '高考', '中考', '职业认证', '公务员', '其他']
      },
      {
        key: '置业安家',
        label: '置业安家',
        icon: Home,
        color: 'text-emerald-500',
        children: ['业主', '装修', '房屋租赁', '房屋出售']
      },
      {
        key: '品牌产品',
        label: '品牌产品',
        icon: Package,
        color: 'text-emerald-500'
      }
    ]
  }
]

const GROUP_AVATARS = [
  { id: 'blue', text: 'Q', bg: '#12a4ff' },
  { id: 'green', text: 'G', bg: '#18c98b' },
  { id: 'orange', text: 'C', bg: '#ff9f1a' },
  { id: 'pink', text: 'N', bg: '#fb6f92' },
  { id: 'cyan', text: 'T', bg: '#12c9bd' },
  { id: 'purple', text: '群', bg: '#8b7cf6' }
]

function avatarDataUrl(avatar: (typeof GROUP_AVATARS)[number]) {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 80 80"><rect width="80" height="80" rx="40" fill="${avatar.bg}"/><text x="40" y="50" text-anchor="middle" font-family="Arial, sans-serif" font-size="30" font-weight="700" fill="white">${avatar.text}</text></svg>`
  return `data:image/svg+xml;utf8,${encodeURIComponent(svg)}`
}

function randomAvatarId() {
  return GROUP_AVATARS[Math.floor(Math.random() * GROUP_AVATARS.length)].id
}

function getAvatar(id: string) {
  return GROUP_AVATARS.find((avatar) => avatar.id === id) ?? GROUP_AVATARS[0]
}

function flattenCategoryLabels() {
  return GROUP_CATEGORY_SECTIONS.flatMap((section) =>
    section.options.flatMap((option) => [option.label, ...(option.children ?? [])])
  )
}

function contactMatches(contact: Contact, query: string) {
  const q = query.trim().toLowerCase()
  if (!q) return true
  return [contact.nickname, contact.remark, contact.id].some((value) => value?.toLowerCase().includes(q))
}

export function CreateGroupModal({ open, onClose }: CreateGroupModalProps) {
  const navigate = useNavigate()
  const contacts = useContactStore((state) => state.contacts)
  const addGroup = useContactStore((state) => state.addGroup)
  const sessions = useSessionStore((state) => state.sessions)
  const [step, setStep] = useState<CreateStep>('select')
  const [selectedCategory, setSelectedCategory] = useState('')
  const [expandedKey, setExpandedKey] = useState<string | null>(null)
  const [memberSearch, setMemberSearch] = useState('')
  const [selectedMemberIds, setSelectedMemberIds] = useState<string[]>([])
  const [name, setName] = useState('')
  const [avatarId, setAvatarId] = useState(randomAvatarId)
  const [agreed, setAgreed] = useState(true)
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState('')

  useEffect(() => {
    if (!open) {
      setStep('select')
      setSelectedCategory('')
      setExpandedKey(null)
      setMemberSearch('')
      setSelectedMemberIds([])
      setName('')
      setAvatarId(randomAvatarId())
      setAgreed(true)
      setError('')
      setLoading(false)
    }
  }, [open])

  useEffect(() => {
    if (!open) return
    function onKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') onClose()
    }
    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
  }, [open, onClose])

  const selectedAvatar = useMemo(() => getAvatar(avatarId), [avatarId])
  const categoryLabels = useMemo(() => flattenCategoryLabels(), [])
  const contactById = useMemo(() => new Map(contacts.map((contact) => [contact.id, contact])), [contacts])
  const recentContacts = useMemo(() => {
    const ids = sessions
      .filter((session) => session.type === 'private')
      .map((session) => session.id)
      .filter((id, index, list) => list.indexOf(id) === index)
    return ids.map((id) => contactById.get(id)).filter((contact): contact is Contact => Boolean(contact))
  }, [contactById, sessions])
  const fallbackContacts = useMemo(
    () => contacts.filter((contact) => !recentContacts.some((recent) => recent.id === contact.id)),
    [contacts, recentContacts]
  )
  const visibleRecentContacts = useMemo(
    () => recentContacts.filter((contact) => contactMatches(contact, memberSearch)),
    [memberSearch, recentContacts]
  )
  const visibleOtherContacts = useMemo(
    () => fallbackContacts.filter((contact) => contactMatches(contact, memberSearch)),
    [fallbackContacts, memberSearch]
  )
  const selectedMembers = useMemo(
    () => selectedMemberIds.map((id) => contactById.get(id)).filter((contact): contact is Contact => Boolean(contact)),
    [contactById, selectedMemberIds]
  )

  function toggleMember(id: string) {
    setSelectedMemberIds((ids) => (ids.includes(id) ? ids.filter((item) => item !== id) : [...ids, id]))
  }

  function selectCategory(category: string) {
    setSelectedCategory(category)
    setStep('info')
  }

  async function handleCreate() {
    const memberIds = selectedMembers.map((member) => member.id)
    const isCategoryCreate = Boolean(selectedCategory)
    const groupName = isCategoryCreate
      ? name.trim()
      : name.trim() || selectedMembers.slice(0, 3).map((member) => member.nickname).join('、') || '新群聊'
    if (isCategoryCreate && (!groupName || !agreed)) return
    if (!isCategoryCreate && memberIds.length === 0) return

    setLoading(true)
    setError('')
    try {
      const ack = await createGroup(groupName, memberIds, selectedCategory ? `群分类：${selectedCategory}` : undefined)
      if (ack.status === 'error') throw new Error(ack.error?.message || '创建群聊失败')
      const groupId = ack.payload?.groupId || `g-${Date.now()}`
      const avatar = avatarDataUrl(selectedAvatar)
      const tags = selectedCategory ? [selectedCategory] : []
      addGroup({
        id: groupId,
        nickname: groupName,
        avatar,
        status: 'online',
        signature: selectedCategory ? '在群里，发现更多～' : '新创建的群聊',
        category: selectedCategory || undefined,
        tags,
        announcement: selectedCategory ? `群分类：${selectedCategory}` : undefined,
        memberCount: memberIds.length + 1,
        members: selectedMembers
      })
      useSessionStore.getState().upsertSession({
        id: groupId,
        type: 'group',
        name: groupName,
        avatar,
        unread: 0,
        pinned: false,
        members: selectedMembers
      })
      useSessionStore.getState().setActiveSession(groupId)
      useUIStore.getState().setActiveRoute('/messages')
      navigate('/messages')
      onClose()
    } catch (err) {
      setError(err instanceof Error ? err.message : '创建失败')
    } finally {
      setLoading(false)
    }
  }

  if (!open) return null

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/35 p-4"
      onClick={(event) => {
        if (event.target === event.currentTarget) onClose()
      }}
    >
      <div
        className={cn(
          'flex overflow-hidden rounded-md bg-[var(--qq-bg)] shadow-[var(--qq-shadow)]',
          step === 'select' && 'h-[544px] w-[538px]',
          step === 'category' && 'max-h-[540px] w-[600px]',
          step === 'info' && 'h-[544px] w-[538px]'
        )}
      >
        {step === 'select' ? (
          <>
            <aside className="flex w-[262px] shrink-0 flex-col border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)]">
              <div className="p-5 pb-3">
                <label className="flex h-8 items-center gap-1.5 rounded-lg bg-[var(--qq-bg)] px-2.5 text-[var(--qq-text-tertiary)]">
                  <Search size={15} />
                  <input
                    autoFocus
                    value={memberSearch}
                    onChange={(event) => setMemberSearch(event.target.value)}
                    placeholder="搜索"
                    className="min-w-0 flex-1 bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
                  />
                </label>
              </div>

              <button
                type="button"
                onClick={() => setStep('category')}
                className="flex h-[52px] items-center justify-between px-5 text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-tertiary)]"
              >
                <span className="font-medium">按分类创建</span>
                <span className="flex items-center gap-1 text-xs text-[var(--qq-text-secondary)]">
                  更多
                  <ChevronRight size={14} />
                </span>
              </button>

              <div className="border-y border-[var(--qq-border)] bg-[var(--qq-bg)] px-5 py-3 text-sm font-medium text-[var(--qq-text)]">
                选择好友创建
              </div>

              <div className="min-h-0 flex-1 overflow-y-auto px-5 py-3">
                <div className="mb-2 flex items-center gap-1.5 text-sm font-medium text-[var(--qq-text)]">
                  <ChevronDown size={14} />
                  最近聊天
                </div>
                <MemberList contacts={visibleRecentContacts} selectedIds={selectedMemberIds} onToggle={toggleMember} />
                {visibleRecentContacts.length === 0 ? (
                  <p className="py-7 text-center text-xs text-[var(--qq-text-tertiary)]">暂无最近聊天</p>
                ) : null}

                {visibleOtherContacts.length > 0 ? (
                  <>
                    <div className="mb-2 mt-4 text-sm font-medium text-[var(--qq-text)]">我的好友</div>
                    <MemberList contacts={visibleOtherContacts} selectedIds={selectedMemberIds} onToggle={toggleMember} />
                  </>
                ) : null}
              </div>
            </aside>

            <section className="flex min-w-0 flex-1 flex-col bg-[var(--qq-bg)]">
              <header className="relative h-12 px-5 pt-5 text-sm font-medium text-[var(--qq-text)]">
                创建群聊
                <button
                  type="button"
                  onClick={onClose}
                  className="absolute right-3 top-3 rounded p-1 text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-secondary)] hover:text-[var(--qq-text)]"
                  aria-label="关闭"
                >
                  <X size={15} />
                </button>
              </header>
              <div className="min-h-0 flex-1 px-5 py-3">
                {selectedMembers.length > 0 ? (
                  <div className="flex flex-wrap gap-2">
                    {selectedMembers.map((member) => (
                      <button
                        type="button"
                        key={member.id}
                        onClick={() => toggleMember(member.id)}
                        className="flex items-center gap-1.5 rounded-full bg-[var(--qq-bg-secondary)] py-1 pl-1 pr-2 text-xs text-[var(--qq-text)] hover:bg-[var(--qq-bg-tertiary)]"
                      >
                        <Avatar src={member.avatar} fallback={member.nickname} size={22} />
                        {member.nickname}
                        <X size={12} className="text-[var(--qq-text-tertiary)]" />
                      </button>
                    ))}
                  </div>
                ) : null}
              </div>
              {error ? <p className="px-5 pb-2 text-xs text-[var(--qq-danger)]">{error}</p> : null}
              <footer className="flex h-16 items-center justify-end gap-2 px-5">
                <button
                  type="button"
                  onClick={handleCreate}
                  disabled={selectedMemberIds.length === 0 || loading}
                  className="flex h-9 min-w-[78px] items-center justify-center gap-1.5 rounded-lg bg-[var(--qq-primary)] px-5 text-sm font-medium text-white hover:bg-[var(--qq-primary-hover)] disabled:bg-[var(--qq-primary)]/35"
                >
                  {loading ? <Loader2 size={14} className="animate-spin" /> : null}
                  确定
                </button>
                <button
                  type="button"
                  onClick={onClose}
                  disabled={loading}
                  className="h-9 min-w-[78px] rounded-lg border border-[var(--qq-border)] bg-[var(--qq-bg)] px-5 text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)] disabled:opacity-60"
                >
                  取消
                </button>
              </footer>
            </section>
          </>
        ) : (
          <section className="flex min-w-0 flex-1 flex-col bg-[var(--qq-bg-secondary)]">
            <header className="relative flex h-10 items-center justify-center border-b border-[var(--qq-border)] bg-[var(--qq-bg)] text-sm font-medium text-[var(--qq-text)]">
              {step === 'category' ? '按分类创建' : '填写群信息'}
              <button
                type="button"
                onClick={() => (step === 'category' ? setStep('select') : setStep('category'))}
                className="absolute left-3 top-2 rounded p-1 text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
                aria-label="返回"
              >
                <ChevronLeft size={16} />
              </button>
              <button
                type="button"
                onClick={onClose}
                className="absolute right-3 top-2 rounded p-1 text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)] hover:text-[var(--qq-text)]"
                aria-label="关闭"
              >
                <X size={16} />
              </button>
            </header>

            {step === 'category' ? (
              <div className="max-h-[500px] overflow-y-auto p-3">
                <div className="space-y-3">
                  {GROUP_CATEGORY_SECTIONS.map((section) => (
                    <section key={section.title} className="rounded-md bg-[var(--qq-bg)] p-4">
                      <h4 className="mb-4 text-sm font-medium text-[var(--qq-text)]">{section.title}</h4>
                      <div className="grid grid-cols-4 gap-3">
                        {section.options.map((option) => {
                          const Icon = option.icon
                          const expanded = expandedKey === option.key
                          return (
                            <button
                              type="button"
                              key={option.key}
                              onClick={() => {
                                if (option.children) setExpandedKey(expanded ? null : option.key)
                                else selectCategory(option.label)
                              }}
                              className={cn(
                                'flex h-9 items-center justify-center gap-1.5 whitespace-nowrap rounded-md px-2 text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)]',
                                expanded && 'border border-[var(--qq-text)] bg-[var(--qq-bg)]'
                              )}
                            >
                              <Icon size={16} className={option.color} />
                              {option.label}
                              {option.children ? <ChevronDown size={12} className={cn('transition-transform', expanded && 'rotate-180')} /> : null}
                            </button>
                          )
                        })}
                      </div>
                      {section.options.map((option) =>
                        option.children && expandedKey === option.key ? (
                          <div key={`${option.key}-children`} className="mt-4 grid grid-cols-4 gap-3">
                            {option.children.map((child) => (
                              <button
                                type="button"
                                key={child}
                                onClick={() => selectCategory(child)}
                                className="h-9 rounded bg-[var(--qq-bg-secondary)] px-2 text-sm text-[var(--qq-text-secondary)] hover:bg-[var(--qq-primary-soft)] hover:text-[var(--qq-primary)]"
                              >
                                {child}
                              </button>
                            ))}
                          </div>
                        ) : null
                      )}
                    </section>
                  ))}
                </div>
              </div>
            ) : (
              <div className="flex min-h-0 flex-1 flex-col overflow-y-auto p-4">
                <p className="mb-2 text-xs text-[var(--qq-text-secondary)]">群名称和群头像</p>
                <section className="rounded-lg bg-[var(--qq-bg)] p-4">
                  <label className="flex h-10 items-center gap-4 rounded-lg bg-[var(--qq-bg-secondary)] px-3">
                    <span className="shrink-0 text-sm font-medium text-[var(--qq-text)]">群名称</span>
                    <input
                      autoFocus
                      value={name}
                      onChange={(event) => setName(event.target.value.slice(0, 32))}
                      placeholder="填写群名称（2-32个字）"
                      className="min-w-0 flex-1 bg-transparent text-sm text-[var(--qq-text)] outline-none placeholder:text-[var(--qq-text-tertiary)]"
                    />
                  </label>
                  <div className="mt-4">
                    <div className="mb-3 text-sm font-medium text-[var(--qq-text)]">群头像</div>
                    <div className="flex flex-wrap gap-4">
                      <button
                        type="button"
                        onClick={() => setAvatarId(randomAvatarId())}
                        className="flex h-10 w-10 items-center justify-center rounded-full bg-[var(--qq-bg-secondary)] text-[var(--qq-text-secondary)] hover:bg-[var(--qq-bg-tertiary)]"
                        aria-label="随机群头像"
                      >
                        <Plus size={20} />
                      </button>
                      {GROUP_AVATARS.map((avatar) => (
                        <button
                          type="button"
                          key={avatar.id}
                          onClick={() => setAvatarId(avatar.id)}
                          className={cn(
                            'relative flex h-10 w-10 items-center justify-center rounded-full text-sm font-bold text-white transition-transform hover:scale-105',
                            avatar.id === avatarId && 'ring-2 ring-[var(--qq-primary)] ring-offset-2 ring-offset-[var(--qq-bg)]'
                          )}
                          style={{ backgroundColor: avatar.bg }}
                          aria-label={`选择头像 ${avatar.id}`}
                        >
                          <span>{avatar.text}</span>
                          {avatar.id === avatarId ? (
                            <span className="absolute -bottom-0.5 -right-0.5 flex h-4 w-4 items-center justify-center rounded-full bg-[var(--qq-primary)] text-white">
                              <Check size={10} />
                            </span>
                          ) : null}
                        </button>
                      ))}
                    </div>
                  </div>
                </section>

                <section className="mt-3 rounded-lg bg-[var(--qq-bg)] p-4">
                  <div className="flex items-center justify-between text-sm">
                    <span className="text-[var(--qq-text-secondary)]">群分类</span>
                    <button type="button" onClick={() => setStep('category')} className="text-[var(--qq-primary)] hover:underline">
                      {selectedCategory || '重新选择'}
                    </button>
                  </div>
                  <div className="mt-3 flex flex-wrap gap-2">
                    {categoryLabels.slice(0, 10).map((label) => (
                      <button
                        type="button"
                        key={label}
                        onClick={() => setSelectedCategory(label)}
                        className={cn(
                          'rounded-md px-3 py-1.5 text-xs transition-colors',
                          selectedCategory === label
                            ? 'bg-[var(--qq-primary-soft)] text-[var(--qq-primary)]'
                            : 'bg-[var(--qq-bg-secondary)] text-[var(--qq-text-secondary)] hover:text-[var(--qq-text)]'
                        )}
                      >
                        {label}
                      </button>
                    ))}
                  </div>
                </section>

                <p className="mt-4 text-xs leading-5 text-[var(--qq-text-tertiary)]">
                  理性追星不盲从，文明表达互尊重，违法违规立举报，社群公约共遵守。
                </p>
                <label className="mt-8 flex items-start gap-2 text-xs leading-5 text-[var(--qq-text-secondary)]">
                  <button
                    type="button"
                    onClick={() => setAgreed((value) => !value)}
                    className={cn(
                      'mt-0.5 flex h-4 w-4 shrink-0 items-center justify-center rounded-full border',
                      agreed ? 'border-[var(--qq-primary)] bg-[var(--qq-primary)] text-white' : 'border-[var(--qq-border)] bg-[var(--qq-bg)]'
                    )}
                    aria-label="同意群聊服务声明"
                  >
                    {agreed ? <Check size={11} /> : null}
                  </button>
                  <span>
                    已阅读并同意 <button type="button" className="text-[var(--qq-primary)] hover:underline">《服务声明》</button>。
                    根据主管部门要求，未成年人禁止担任粉丝群的群主/管理员，经核实将按群相关规则进行处理。
                  </span>
                </label>
                {error ? <p className="mt-3 text-xs text-[var(--qq-danger)]">{error}</p> : null}
              </div>
            )}

            {step === 'info' ? (
              <footer className="flex items-center justify-between border-t border-[var(--qq-border)] bg-[var(--qq-bg)] px-3 py-3">
                <button
                  type="button"
                  onClick={() => setStep('category')}
                  disabled={loading}
                  className="rounded-lg border border-[var(--qq-border)] px-6 py-2 text-sm text-[var(--qq-text)] hover:bg-[var(--qq-bg-secondary)] disabled:opacity-60"
                >
                  上一步
                </button>
                <button
                  type="button"
                  onClick={handleCreate}
                  disabled={name.trim().length < 2 || !selectedCategory || !agreed || loading}
                  className="flex items-center gap-1.5 rounded-lg bg-[var(--qq-primary)] px-6 py-2 text-sm font-medium text-white hover:bg-[var(--qq-primary-hover)] disabled:bg-[var(--qq-primary)]/35"
                >
                  {loading ? <Loader2 size={14} className="animate-spin" /> : null}
                  立即创建
                </button>
              </footer>
            ) : null}
          </section>
        )}
      </div>
    </div>
  )
}

function MemberList({
  contacts,
  selectedIds,
  onToggle
}: {
  contacts: Contact[]
  selectedIds: string[]
  onToggle: (id: string) => void
}) {
  return (
    <div className="space-y-1">
      {contacts.map((contact) => {
        const selected = selectedIds.includes(contact.id)
        return (
          <button
            type="button"
            key={contact.id}
            onClick={() => onToggle(contact.id)}
            className="flex w-full items-center gap-3 rounded-md py-1.5 text-left hover:bg-[var(--qq-bg-tertiary)]"
          >
            <span
              className={cn(
                'flex h-4 w-4 shrink-0 items-center justify-center rounded-full border',
                selected ? 'border-[var(--qq-primary)] bg-[var(--qq-primary)] text-white' : 'border-[var(--qq-border)] bg-[var(--qq-bg)]'
              )}
            >
              {selected ? <Check size={10} /> : null}
            </span>
            <Avatar src={contact.avatar} fallback={contact.nickname} size={32} />
            <span className="min-w-0 flex-1 truncate text-sm text-[var(--qq-text)]">{contact.remark || contact.nickname}</span>
          </button>
        )
      })}
    </div>
  )
}

