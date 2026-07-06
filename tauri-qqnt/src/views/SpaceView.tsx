import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Globe } from 'lucide-react'

export function SpaceView() {
  return (
    <MockPlaceholder
      title="空间"
      icon={<Globe size={28} strokeWidth={1.5} />}
    />
  )
}