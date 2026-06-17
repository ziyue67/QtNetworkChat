import { Users } from 'lucide-react'
import { MockPlaceholder } from '@/components/common/MockPlaceholder'

export function ContactsView() {
  return (
    <MockPlaceholder
      title="联系人"
      icon={<Users size={28} strokeWidth={1.5} />}
    />
  )
}