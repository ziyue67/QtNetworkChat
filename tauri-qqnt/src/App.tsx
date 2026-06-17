import { HashRouter, Routes, Route, Navigate } from 'react-router-dom'
import { useEffect, useState } from 'react'
import { useAuthStore } from '@/stores/authStore'
import { useTheme } from '@/hooks/useTheme'
import { TitleBar } from '@/components/frame/TitleBar'
import { MainLayout } from '@/views/MainLayout'
import { LoginView } from '@/views/LoginView'
import { MessageView } from '@/views/MessageView'
import { ContactsView } from '@/views/ContactsView'
import { SettingsView } from '@/views/SettingsView'
import { ProfileView } from '@/views/ProfileView'
import { SpaceView } from '@/views/SpaceView'
import { ChannelView } from '@/views/ChannelView'
import { MailView } from '@/views/MailView'
import { DocsView } from '@/views/DocsView'
import { CalendarView } from '@/views/CalendarView'
import { MeetingView } from '@/views/MeetingView'
import { FavoritesView } from '@/views/FavoritesView'
import { WalletView } from '@/views/WalletView'
import './styles/index.css'

const LOGIN_SIZE = { width: 300, height: 460 }
const LOGIN_MIN_SIZE = { width: 300, height: 460 }
const MAIN_SIZE = { width: 1100, height: 740 }
const MAIN_MIN_SIZE = { width: 860, height: 540 }

function useResizeForAuth(isAuthenticated: boolean) {
  useEffect(() => {
    let mounted = true

    async function resize() {
      try {
        const [{ getCurrentWindow }, { LogicalSize }] = await Promise.all([
          import('@tauri-apps/api/window'),
          import('@tauri-apps/api/dpi')
        ])
        const win = getCurrentWindow()
        const size = isAuthenticated ? MAIN_SIZE : LOGIN_SIZE
        const minSize = isAuthenticated ? MAIN_MIN_SIZE : LOGIN_MIN_SIZE

        if (!mounted) return
        await win.setResizable(isAuthenticated)
        await win.setMinSize(new LogicalSize(minSize.width, minSize.height))
        await win.setSize(new LogicalSize(size.width, size.height))
        if (isAuthenticated) {
          await win.center()
        }
      } catch {
        // Not running inside Tauri (e.g. browser preview).
      }
    }

    resize()
    return () => {
      mounted = false
    }
  }, [isAuthenticated])
}

function App() {
  useTheme()
  const isAuthenticated = useAuthStore((state) => state.isAuthenticated)
  const [ready, setReady] = useState(false)

  useEffect(() => {
    setReady(true)
  }, [])

  useResizeForAuth(isAuthenticated)

  if (!ready) {
    return <div className="h-full w-full bg-[var(--qq-bg)]" />
  }

  return (
    <HashRouter>
      <Routes>
        <Route
          path="/login"
          element={<LoginScreen />}
        />
        <Route
          path="/"
          element={isAuthenticated ? <MainLayout /> : <Navigate to="/login" replace />}
        >
          <Route index element={<Navigate to="/messages" replace />} />
          <Route path="messages" element={<MessageView />} />
          <Route path="contacts" element={<ContactsView />} />
          <Route path="spaces" element={<SpaceView />} />
          <Route path="channels" element={<ChannelView />} />
          <Route path="mail" element={<MailView />} />
          <Route path="docs" element={<DocsView />} />
          <Route path="calendar" element={<CalendarView />} />
          <Route path="meetings" element={<MeetingView />} />
          <Route path="favorites" element={<FavoritesView />} />
          <Route path="wallet" element={<WalletView />} />
          <Route path="settings" element={<SettingsView />} />
          <Route path="profile" element={<ProfileView />} />
        </Route>
      </Routes>
    </HashRouter>
  )
}

function LoginScreen() {
  const isAuthenticated = useAuthStore((state) => state.isAuthenticated)
  if (isAuthenticated) {
    return <Navigate to="/messages" replace />
  }
  return (
    <div className="flex h-full w-full flex-col overflow-hidden bg-[var(--qq-bg)]">
      <TitleBar variant="close-only" />
      <LoginView />
    </div>
  )
}

export default App
