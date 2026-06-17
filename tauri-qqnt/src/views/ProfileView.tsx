import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { User } from 'lucide-react'

export function ProfileView() {
  return (
    <MockPlaceholder
      title="个人资料"
      icon={<User size={28} strokeWidth={1.5} />}
    />
  )
}