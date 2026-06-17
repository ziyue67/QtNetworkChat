import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Settings } from 'lucide-react'

export function SettingsView() {
  return (
    <MockPlaceholder
      title="设置"
      icon={<Settings size={28} strokeWidth={1.5} />}
    />
  )
}