import { useEffect, useRef } from 'react'
import { emit, listen } from '@tauri-apps/api/event'
import { isRegistered, register, unregister } from '@tauri-apps/plugin-global-shortcut'
import { DEFAULT_SCREENSHOT_SHORTCUT, normalizeShortcut } from '@/lib/shortcut'

const SCREENSHOT_SHORTCUT = DEFAULT_SCREENSHOT_SHORTCUT
const SCREENSHOT_EVENT = 'qqnt://screenshot/shortcut'

export function useScreenshotShortcut(enabled: boolean, shortcut = SCREENSHOT_SHORTCUT) {
  const enabledRef = useRef(enabled)
  const normalizedShortcut = normalizeShortcut(shortcut)

  useEffect(() => {
    enabledRef.current = enabled
  }, [enabled])

  useEffect(() => {
    let cancelled = false
    let registered = false

    async function bindShortcut() {
      try {
        if (await isRegistered(normalizedShortcut)) {
          await unregister(normalizedShortcut).catch(() => undefined)
        }

        await register(normalizedShortcut, (event) => {
          // Snow Shot waits for key release so the shortcut does not fire twice.
          if (event.state !== 'Released' || !enabledRef.current) return
          void emit(SCREENSHOT_EVENT)
        })

        registered = true
        if (cancelled) {
          await unregister(normalizedShortcut).catch(() => undefined)
          registered = false
        }
      } catch (error) {
        console.warn('截图快捷键注册失败', error)
      }
    }

    void bindShortcut()

    return () => {
      cancelled = true
      if (registered) {
        void unregister(normalizedShortcut).catch(() => undefined)
      }
    }
  }, [normalizedShortcut])
}

export async function onScreenshotShortcut(handler: () => void | Promise<void>) {
  return listen(SCREENSHOT_EVENT, () => {
    void handler()
  })
}

export { SCREENSHOT_SHORTCUT }
