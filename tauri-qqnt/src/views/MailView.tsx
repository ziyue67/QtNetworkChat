import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Mail } from 'lucide-react'

export function MailView() {
  return (
    <MockPlaceholder
      title="邮件"
      icon={<Mail size={28} strokeWidth={1.5} />}
    />
  )
}