import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Star } from 'lucide-react'

export function FavoritesView() {
  return (
    <MockPlaceholder
      title="收藏"
      icon={<Star size={28} strokeWidth={1.5} />}
    />
  )
}