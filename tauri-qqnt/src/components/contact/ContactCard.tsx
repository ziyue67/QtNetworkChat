import { Avatar } from '@/components/common/Avatar'
import { MessageCircle, Users, Megaphone } from 'lucide-react'
import type { Contact } from '@/types/qqnt'

const STATUS_MAP: Record<string, string> = {
  online: '在线',
  offline: '离线',
  busy: '忙碌',
  away: '离开'
}

interface ContactCardProps {
  contact: Contact
  onSendMessage?: (contact: Contact) => void
  isGroup?: boolean
}

export function ContactCard({ contact, onSendMessage, isGroup = false }: ContactCardProps) {
  const members = contact.members || []
  const memberCount = contact.memberCount ?? members.length

  return (
    <div className="flex h-full flex-col bg-[var(--qq-bg)]">
      <div className="flex flex-1 flex-col items-center justify-center p-8 text-center">
        <Avatar
          src={contact.avatar}
          fallback={contact.nickname}
          size={96}
          className="mb-4 h-24 w-24 text-3xl"
        />
        <h2 className="text-xl font-semibold text-[var(--qq-text)]">{contact.nickname}</h2>
        <p className="mt-1 text-sm text-[var(--qq-text-secondary)]">
          {isGroup ? '群聊' : STATUS_MAP[contact.status] || '离线'}
        </p>
        {contact.signature ? (
          <p className="mt-3 max-w-xs text-sm text-[var(--qq-text-tertiary)]">{contact.signature}</p>
        ) : null}
        {contact.remark ? (
          <p className="mt-1 text-xs text-[var(--qq-text-tertiary)]">备注：{contact.remark}</p>
        ) : null}
        {isGroup ? (
          <div className="mt-6 w-full max-w-md rounded-xl border border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] p-4 text-left">
            <div className="flex items-center gap-2 text-sm font-medium text-[var(--qq-text)]">
              <Megaphone size={15} />
              群公告
            </div>
            <p className="mt-2 text-xs leading-5 text-[var(--qq-text-secondary)]">
              {contact.announcement || contact.signature || '暂无群公告'}
            </p>
            <div className="mt-4 flex items-center justify-between text-sm font-medium text-[var(--qq-text)]">
              <span>群成员</span>
              <span className="text-xs font-normal text-[var(--qq-text-tertiary)]">{memberCount} 人</span>
            </div>
            {members.length > 0 ? (
              <div className="mt-3 grid grid-cols-4 gap-3">
                {members.slice(0, 12).map((member) => (
                  <div key={member.id} className="min-w-0 text-center">
                    <Avatar src={member.avatar} fallback={member.nickname} size={34} className="mx-auto" />
                    <p className="mt-1 truncate text-[10px] text-[var(--qq-text-secondary)]">{member.nickname}</p>
                  </div>
                ))}
              </div>
            ) : (
              <p className="mt-3 text-xs text-[var(--qq-text-tertiary)]">等待后端同步成员列表</p>
            )}
          </div>
        ) : null}
      </div>
      <div className="border-t border-[var(--qq-border)] p-4">
        <button
          onClick={() => onSendMessage?.(contact)}
          className="flex w-full items-center justify-center gap-2 rounded-md bg-[var(--qq-primary)] py-2.5 text-sm font-medium text-white transition-colors hover:bg-[var(--qq-primary-hover)]"
        >
          {isGroup ? <Users size={16} /> : <MessageCircle size={16} />}
          {isGroup ? '发送群消息' : '发送消息'}
        </button>
      </div>
    </div>
  )
}
