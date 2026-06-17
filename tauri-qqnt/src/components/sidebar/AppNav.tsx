import { MessageSquare, Users, Globe, Radio, Mail, FileText, CalendarDays, Video, Star, Wallet, Settings } from 'lucide-react'
import { NavItem } from './NavItem'
import { useUIStore } from '@/stores/uiStore'
import { useNavigate } from 'react-router-dom'

const NAV_ITEMS = [
  { route: '/messages', label: '消息', icon: MessageSquare },
  { route: '/contacts', label: '联系人', icon: Users },
  { route: '/spaces', label: '空间', icon: Globe, mock: true },
  { route: '/channels', label: '频道', icon: Radio, mock: true },
  { route: '/mail', label: '邮件', icon: Mail, mock: true },
  { route: '/docs', label: '文档', icon: FileText, mock: true },
  { route: '/calendar', label: '日历', icon: CalendarDays, mock: true },
  { route: '/meetings', label: '会议', icon: Video, mock: true },
  { route: '/favorites', label: '收藏', icon: Star, mock: true },
  { route: '/wallet', label: '钱包', icon: Wallet, mock: true },
  { route: '/settings', label: '设置', icon: Settings }
]

export function AppNav() {
  const activeRoute = useUIStore((state) => state.activeRoute)
  const setActiveRoute = useUIStore((state) => state.setActiveRoute)
  const navigate = useNavigate()

  return (
    <nav className="flex h-full w-[var(--qq-sidebar-width)] flex-col items-center gap-1 border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] py-3">
      {NAV_ITEMS.map((item) => (
        <NavItem
          key={item.route}
          icon={<item.icon size={22} strokeWidth={1.5} />}
          label={item.label}
          active={activeRoute === item.route}
          onClick={() => {
            setActiveRoute(item.route)
            navigate(item.route)
          }}
        />
      ))}
    </nav>
  )
}
