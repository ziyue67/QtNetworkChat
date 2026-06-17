import { Outlet } from 'react-router-dom'
import { TitleBar } from '@/components/frame/TitleBar'
import { AppNav } from '@/components/sidebar/AppNav'

export function MainLayout() {
  return (
    <div className="flex h-full w-full flex-col overflow-hidden rounded-lg bg-[var(--qq-bg)]">
      <TitleBar />
      <div className="flex min-h-0 flex-1">
        <AppNav />
        <main className="min-h-0 flex-1">
          <Outlet />
        </main>
      </div>
    </div>
  )
}