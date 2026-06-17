import { useEffect } from 'react'
import { useUIStore } from '@/stores/uiStore'
import type { ThemeMode } from '@/types/qqnt'

function applyTheme(mode: ThemeMode) {
  let resolved: 'light' | 'dark'
  if (mode === 'system') {
    resolved = window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'
  } else {
    resolved = mode
  }
  document.documentElement.classList.toggle('dark', resolved === 'dark')
}

export function useTheme() {
  const theme = useUIStore((state) => state.theme)

  useEffect(() => {
    applyTheme(theme)
    if (theme !== 'system') return
    const listener = (e: MediaQueryListEvent) => {
      document.documentElement.classList.toggle('dark', e.matches)
    }
    const mq = window.matchMedia('(prefers-color-scheme: dark)')
    mq.addEventListener('change', listener)
    return () => mq.removeEventListener('change', listener)
  }, [theme])

  return theme
}