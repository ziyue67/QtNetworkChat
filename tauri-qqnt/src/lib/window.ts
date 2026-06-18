import type { MouseEvent } from 'react'
import { getCurrentWindow } from '@tauri-apps/api/window'

const INTERACTIVE_SELECTOR = 'button, input, textarea, select, a, [data-no-drag]'

export function startWindowDrag(event: MouseEvent<HTMLElement>) {
  if (event.button !== 0) return
  if (event.target instanceof HTMLElement && event.target.closest(INTERACTIVE_SELECTOR)) return

  void getCurrentWindow().startDragging().catch(() => undefined)
}
