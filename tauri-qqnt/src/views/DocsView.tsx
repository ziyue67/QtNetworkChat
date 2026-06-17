import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { FileText } from 'lucide-react'

export function DocsView() {
  return (
    <MockPlaceholder
      title="文档"
      icon={<FileText size={28} strokeWidth={1.5} />}
    />
  )
}