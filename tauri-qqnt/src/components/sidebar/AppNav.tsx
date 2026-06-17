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
import { useSessionStore } from '@/stores/sessionStore'
import { APP_ENTRIES, type AppIconName } from '@/config/appEntries'
import { useNavigate } from 'react-router-dom'

const ICONS: Record<AppIconName, typeof MessageSquare> = {
  MessageIcon: MessageSquare,
  ContactsIcon: Users,
  SpaceIcon: Globe,
  ChannelIcon: Radio,
  MailIcon: Mail,
  DocsIcon: FileText,
  CalendarIcon: CalendarDays,
  MeetingIcon: Video,
  FavoritesIcon: Star,
  WalletIcon: Wallet,
  SettingsIcon: Settings
}

export function AppNav() {
  const activeRoute = useUIStore((state) => state.activeRoute)
  const setActiveRoute = useUIStore((state) => state.setActiveRoute)
  const unreadTotal = useSessionStore((state) => state.sessions.reduce((sum, session) => sum + session.unread, 0))
  const navigate = useNavigate()

  return (
    <nav className="flex h-full w-[var(--qq-sidebar-width)] flex-col items-center gap-1 border-r border-[var(--qq-border)] bg-[var(--qq-bg-secondary)] py-3">
      {APP_ENTRIES.map((item) => {
        const Icon = ICONS[item.icon]
        return (
          <NavItem
            key={item.path}
            icon={<Icon size={22} strokeWidth={1.5} />}
            label={item.label}
            active={activeRoute === item.path}
            badge={item.id === 'messages' ? unreadTotal : undefined}
            mock={item.mock}
            onClick={() => {
              setActiveRoute(item.path)
              navigate(item.path)
            }}
          />
        )
      })}
    </nav>
  )
}
