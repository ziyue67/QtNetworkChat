import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { CalendarDays } from 'lucide-react'

export function CalendarView() {
  return (
    <MockPlaceholder
      title="日历"
      icon={<CalendarDays size={28} strokeWidth={1.5} />}
    />
  )
}