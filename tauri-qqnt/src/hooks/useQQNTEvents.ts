import { listen } from '@tauri-apps/api/event'
import { useEffect } from 'react'
import type { QQNTEvent } from '@/types/qqnt'

export function useQQNTEvents<T = unknown>(
  event: string,
  handler: (payload: QQNTEvent<T>) => void
) {
  useEffect(() => {
    let unmounted = false
    let cleanup: (() => void) | undefined

    listen(event, (eventPayload) => {
      if (unmounted) return
      handler({ type: event, payload: eventPayload.payload as T })
    }).then((unlisten) => {
      if (unmounted) {
        unlisten()
        return
      }
      cleanup = unlisten
    })

    return () => {
      unmounted = true
      cleanup?.()
    }
  }, [event, handler])
}