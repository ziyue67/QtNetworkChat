import type { QQNTAppEntry } from '@/types/qqnt'

export type AppIconName =
  | 'MessageIcon'
  | 'ContactsIcon'
  | 'SpaceIcon'
  | 'ChannelIcon'
  | 'MailIcon'
  | 'DocsIcon'
  | 'CalendarIcon'
  | 'MeetingIcon'
  | 'FavoritesIcon'
  | 'WalletIcon'
  | 'SettingsIcon'

export interface AppEntry extends QQNTAppEntry {
  icon: AppIconName
}

export const APP_ENTRIES: AppEntry[] = [
  { id: 'messages', icon: 'MessageIcon', label: '消息', path: '/messages', mock: false },
  { id: 'contacts', icon: 'ContactsIcon', label: '联系人', path: '/contacts', mock: false },
  { id: 'space', icon: 'SpaceIcon', label: '空间', path: '/spaces', mock: true },
  { id: 'channel', icon: 'ChannelIcon', label: '频道', path: '/channels', mock: true },
  { id: 'mail', icon: 'MailIcon', label: '邮件', path: '/mail', mock: true },
  { id: 'docs', icon: 'DocsIcon', label: '文档', path: '/docs', mock: true },
  { id: 'calendar', icon: 'CalendarIcon', label: '日历', path: '/calendar', mock: true },
  { id: 'meeting', icon: 'MeetingIcon', label: '会议', path: '/meetings', mock: true },
  { id: 'favorites', icon: 'FavoritesIcon', label: '收藏', path: '/favorites', mock: true },
  { id: 'wallet', icon: 'WalletIcon', label: '钱包', path: '/wallet', mock: true },
  { id: 'settings', icon: 'SettingsIcon', label: '设置', path: '/settings', mock: false }
]
