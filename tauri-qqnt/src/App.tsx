import { HashRouter, Routes, Route, Navigate } from 'react-router-dom'
import { useEffect, useState } from 'react'
import { useAuthStore } from '@/stores/authStore'
import { useTheme } from '@/hooks/useTheme'
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

function App() {
  useTheme()
  const isAuthenticated = useAuthStore((state) => state.isAuthenticated)
  const [ready, setReady] = useState(false)

  useEffect(() => {
    setReady(true)
  }, [])

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
  return <LoginView />
}

export default App