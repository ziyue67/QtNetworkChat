import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Radio } from 'lucide-react'

export function ChannelView() {
  return (
    <MockPlaceholder
      title="频道"
      icon={<Radio size={28} strokeWidth={1.5} />}
    />
  )
}