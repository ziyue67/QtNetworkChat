import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Video } from 'lucide-react'

export function MeetingView() {
  return (
    <MockPlaceholder
      title="会议"
      icon={<Video size={28} strokeWidth={1.5} />}
    />
  )
}