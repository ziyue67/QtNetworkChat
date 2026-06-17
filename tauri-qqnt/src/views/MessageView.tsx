import { MessageSquare } from 'lucide-react'
import { MockPlaceholder } from '@/components/common/MockPlaceholder'

export function MessageView() {
  return (
    <MockPlaceholder
      title="消息"
      icon={<MessageSquare size={28} strokeWidth={1.5} />}
    />
  )
}