import {
  MessageSquare,
  Users,
  Globe,
  Radio,
  Mail,
  FileText,
  CalendarDays,
  Video,
  Star,
  Wallet,
  Settings
} from 'lucide-react'
import { NavItem } from './NavItem'
import { useUIStore } from '@/stores/uiStore'
import { useNavigate } from 'react-router-dom'

const NAV_ITEMS = [
  { route: '/messages', label: '消息', icon: MessageSquare },
  { route: '/contacts', label: '联系人', icon: Users },
  { route: '/spaces', label: '空间', icon: Globe },
  { route: '/channels', label: '频道', icon: Radio },
  { route: '/mail', label: '邮件', icon: Mail },
  { route: '/docs', label: '文档', icon: FileText },
  { route: '/calendar', label: '日历', icon: CalendarDays },
  { route: '/meetings', label: '会议', icon: Video },
  { route: '/favorites', label: '收藏', icon: Star },
  { route: '/wallet', label: '钱包', icon: Wallet },
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