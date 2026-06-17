import { MockPlaceholder } from '@/components/common/MockPlaceholder'
import { Wallet } from 'lucide-react'

export function WalletView() {
  return (
    <MockPlaceholder
      title="钱包"
      icon={<Wallet size={28} strokeWidth={1.5} />}
    />
  )
}