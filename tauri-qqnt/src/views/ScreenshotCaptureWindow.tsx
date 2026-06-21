import { useEffect, useRef, useState } from 'react'
import { useSearchParams } from 'react-router-dom'
import { invoke } from '@tauri-apps/api/core'
import { emit, listen } from '@tauri-apps/api/event'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { PhysicalPosition, PhysicalSize } from '@tauri-apps/api/dpi'
import { Check, X } from 'lucide-react'

interface ScreenshotResponse {
  filePath?: string
  fileName?: string
  file_path?: string
  file_name?: string
  captureId?: string
  capture_id?: string
  x?: number
  y?: number
  width?: number
  height?: number
}

interface HideMainWindowResponse {
  hidden?: boolean
}

interface SharedBufferEvent {
  getBuffer: () => ArrayBuffer
  additionalData?: string | Record<string, unknown>
}

interface ScreenshotWindowEventPayload {
  requestId?: string
  hideMainWindow?: boolean
}

interface CloseOptions {
  silent?: boolean
  deferClose?: boolean
  keepHiddenMainWindow?: boolean
}

interface Point {
  x: number
  y: number
}

interface Selection {
  x: number
  y: number
  width: number
  height: number
}

interface ViewportSize {
  width: number
  height: number
}

function normalizeSelection(start: Point, end: Point): Selection {
  return {
    x: Math.min(start.x, end.x),
    y: Math.min(start.y, end.y),
    width: Math.abs(end.x - start.x),
    height: Math.abs(end.y - start.y)
  }
}

function responsePath(response?: ScreenshotResponse) {
  return response?.filePath || response?.file_path || ''
}

function responseCaptureId(response?: ScreenshotResponse) {
  return response?.captureId || response?.capture_id || ''
}

function responseName(response?: ScreenshotResponse) {
  return response?.fileName || response?.file_name || ''
}

function supportsSharedBuffer() {
  const chrome = (window as unknown as { chrome?: { webview?: unknown } }).chrome
  return Boolean(chrome?.webview)
}

function releaseSharedBuffer(buffer: ArrayBuffer) {
  const chrome = (window as unknown as {
    chrome?: {
      webview?: {
        releaseBuffer?: (buffer: ArrayBuffer) => void
      }
    }
  }).chrome
  chrome?.webview?.releaseBuffer?.(buffer)
}

function sharedBufferTransferType(event: SharedBufferEvent) {
  const additionalData = event.additionalData
  if (!additionalData) return ''
  if (typeof additionalData === 'string') {
    try {
      const parsed = JSON.parse(additionalData) as { transfer_type?: unknown; transferType?: unknown }
      return String(parsed.transfer_type || parsed.transferType || additionalData)
    } catch {
      return additionalData
    }
  }

  return String(additionalData.transfer_type || additionalData.transferType || '')
}

function waitForSharedBuffer(transferType: string, timeoutMs = 4000) {
  if (!supportsSharedBuffer()) return Promise.reject(new Error('当前 WebView2 不支持 SharedBuffer。'))

  return new Promise<ArrayBuffer>((resolve, reject) => {
    const webview = (window as unknown as {
      chrome: {
        webview: {
          addEventListener: (name: 'sharedbufferreceived', handler: (event: SharedBufferEvent) => void) => void
          removeEventListener: (name: 'sharedbufferreceived', handler: (event: SharedBufferEvent) => void) => void
        }
      }
    }).chrome.webview

    const finish = (buffer?: ArrayBuffer, timedOut = false) => {
      window.clearTimeout(timer)
      webview.removeEventListener('sharedbufferreceived', handleSharedBufferReceived)
      if (buffer) resolve(buffer)
      else reject(new Error(timedOut ? 'WebView2 SharedBuffer 没有返回截图数据。' : 'SharedBuffer 图片数据为空。'))
    }
    const handleSharedBufferReceived = (event: SharedBufferEvent) => {
      if (sharedBufferTransferType(event) !== transferType) return
      finish(event.getBuffer())
    }
    const timer = window.setTimeout(() => finish(undefined, true), timeoutMs)
    webview.addEventListener('sharedbufferreceived', handleSharedBufferReceived)
  })
}

function drawSharedBufferToCanvas(buffer: ArrayBuffer, canvas: HTMLCanvasElement) {
  if (buffer.byteLength <= 8) throw new Error('SharedBuffer 图片数据为空。')

  const infoOffset = buffer.byteLength - 8
  const view = new DataView(buffer, infoOffset, 8)
  const width = view.getUint32(0, true)
  const height = view.getUint32(4, true)
  const pixelLength = width * height * 4
  if (!width || !height || pixelLength > infoOffset) {
    throw new Error('SharedBuffer 图片尺寸无效。')
  }

  const pixels = new Uint8ClampedArray(buffer, 0, pixelLength)
  canvas.width = width
  canvas.height = height
  const context = canvas.getContext('2d')
  if (!context) throw new Error('无法创建截图画布。')

  context.putImageData(new ImageData(pixels, width, height), 0, 0)
  return { width, height }
}

function viewportFromPhysicalSize(width: number, height: number): ViewportSize {
  // Snow Shot uses the WebView devicePixelRatio after the physical window rect is set.
  const scale = window.devicePixelRatio || 1
  return {
    width: Math.max(1, Math.round(width / scale)),
    height: Math.max(1, Math.round(height / scale))
  }
}

async function setWindowRect(width: number, height: number, x: number, y: number) {
  const currentWindow = getCurrentWindow()
  const position = new PhysicalPosition(x, y)
  const size = new PhysicalSize(width, height)

  // Snow Shot also applies the physical rect twice to avoid DPI/scale drift on Windows.
  await Promise.all([
    currentWindow.setPosition(position),
    currentWindow.setSize(size)
  ]).catch(() => undefined)
  await Promise.all([
    currentWindow.setPosition(position),
    currentWindow.setSize(size)
  ]).catch(() => undefined)
}

function waitForNextFrame() {
  return new Promise<void>((resolve) => {
    requestAnimationFrame(() => requestAnimationFrame(() => resolve()))
  })
}

function wait(ms: number) {
  return new Promise<void>((resolve) => window.setTimeout(resolve, ms))
}

async function hideCaptureWindow() {
  await getCurrentWindow().hide().catch(() => undefined)
}

async function closeCaptureWindow(delayMs = 120) {
  const currentWindow = getCurrentWindow()
  await currentWindow.hide().catch(() => undefined)
  if (delayMs > 0) await wait(delayMs)
  await currentWindow.close().catch(() => undefined)
}

const SCREENSHOT_WINDOW_READY_EVENT = 'qqnt://screenshot/window-ready'
const SCREENSHOT_OVERLAY_READY_EVENT = 'qqnt://screenshot/overlay-ready'
const SCREENSHOT_START_EVENT = 'qqnt://screenshot/start'
const SCREENSHOT_FAILED_EVENT = 'qqnt://screenshot/failed'

export function ScreenshotCaptureWindow() {
  const [searchParams] = useSearchParams()
  const initialRequestId = searchParams.get('request') || ''
  const [captureId, setCaptureId] = useState('')
  const [ready, setReady] = useState(false)
  const [error, setError] = useState('')
  const [selection, setSelection] = useState<Selection | null>(null)
  const [saving, setSaving] = useState(false)
  const [viewportSize, setViewportSize] = useState<ViewportSize>({ width: 1, height: 1 })
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const dragStartRef = useRef<Point | null>(null)
  const captureIdRef = useRef('')
  const requestIdRef = useRef(initialRequestId)
  const closingRef = useRef(false)
  const startedRef = useRef(false)
  const hiddenMainWindowRef = useRef(false)
  const restoreTimerRef = useRef<number | undefined>(undefined)

  useEffect(() => {
    let cancelled = false
    let unlistenStart: (() => void) | undefined
    const currentWindow = getCurrentWindow()

    async function restoreMainWindow() {
      if (!hiddenMainWindowRef.current) return
      window.clearTimeout(restoreTimerRef.current)
      restoreTimerRef.current = undefined
      await invoke('restore_main_window').catch(() => undefined)
      hiddenMainWindowRef.current = false
    }

    async function hideMainWindowForCapture() {
      const result = await invoke<HideMainWindowResponse>('hide_main_window').catch(() => undefined)
      hiddenMainWindowRef.current = Boolean(result?.hidden)
      if (!hiddenMainWindowRef.current) return
      window.clearTimeout(restoreTimerRef.current)
      restoreTimerRef.current = window.setTimeout(() => {
        void restoreMainWindow()
      }, 20000)
      await wait(96)
    }

    async function captureAndLoad(nextRequestId = '', hideMainWindow = false) {
      if (startedRef.current) return
      startedRef.current = true
      requestIdRef.current = nextRequestId
      closingRef.current = false
      setError('')
      setReady(false)
      setSelection(null)
      setSaving(false)
      try {
        await hideCaptureWindow()
        await currentWindow.setIgnoreCursorEvents(false).catch(() => undefined)
        if (hideMainWindow) await hideMainWindowForCapture()

        const transferType = nextRequestId ? `screenshot:${nextRequestId}` : 'screenshot'
        const sharedBufferPromise = waitForSharedBuffer(transferType)
        const screenshot = await invoke<ScreenshotResponse>('capture_screenshot_shared_buffer', { requestId: nextRequestId })
        const sharedBuffer = await sharedBufferPromise
        const nextCaptureId = responseCaptureId(screenshot)
        if (!nextCaptureId) throw new Error('截图已完成，但客户端没有返回截图缓存 ID。')

        const canvas = canvasRef.current
        if (!canvas) throw new Error('截图画布还没有准备好。')
        const renderedImage = drawSharedBufferToCanvas(sharedBuffer, canvas)
        releaseSharedBuffer(sharedBuffer)

        if (!cancelled) {
          const physicalWidth = screenshot.width || renderedImage.width
          const physicalHeight = screenshot.height || renderedImage.height
          await setWindowRect(physicalWidth, physicalHeight, screenshot.x || 0, screenshot.y || 0)
          const nextViewportSize = viewportFromPhysicalSize(physicalWidth, physicalHeight)
          captureIdRef.current = nextCaptureId
          setCaptureId(nextCaptureId)
          setViewportSize(nextViewportSize)
          setReady(true)
          await currentWindow.setAlwaysOnTop(true).catch(() => undefined)
          await currentWindow.show()
          await waitForNextFrame()
          await emit(SCREENSHOT_OVERLAY_READY_EVENT, { requestId: requestIdRef.current }).catch(() => undefined)
          await currentWindow.setFocus().catch(() => undefined)
        }
      } catch (err) {
        const message = err instanceof Error ? err.message : '截图加载失败'
        await restoreMainWindow()
        await emit(SCREENSHOT_FAILED_EVENT, { requestId: requestIdRef.current, message }).catch(() => undefined)
        if (!cancelled) await closeCaptureWindow(0)
        startedRef.current = false
      }
    }

    async function prepareAndWaitForStart() {
      try {
        await invoke('prepare_screenshot_window').catch(() => undefined)
        await invoke('set_screenshot_window_exclude_from_capture', { enable: true }).catch(() => undefined)
        await hideCaptureWindow()
        await currentWindow.setIgnoreCursorEvents(false).catch(() => undefined)
        unlistenStart = await listen<ScreenshotWindowEventPayload>(SCREENSHOT_START_EVENT, (event) => {
          void captureAndLoad(event.payload?.requestId || '', Boolean(event.payload?.hideMainWindow))
        })
        await emit(SCREENSHOT_WINDOW_READY_EVENT, { requestId: initialRequestId }).catch(() => undefined)
      } catch (err) {
        const message = err instanceof Error ? err.message : '截图窗口准备失败'
        await emit(SCREENSHOT_FAILED_EVENT, { requestId: initialRequestId, message }).catch(() => undefined)
        if (!cancelled) await closeCaptureWindow(0)
      }
    }

    void prepareAndWaitForStart()

    return () => {
      cancelled = true
      window.clearTimeout(restoreTimerRef.current)
      void restoreMainWindow()
      unlistenStart?.()
    }
  }, [initialRequestId])

  useEffect(() => {
    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === 'Escape') void closeWindow()
      if (event.key === 'Enter') void confirmSelection()
    }

    document.addEventListener('keydown', handleKeyDown)
    return () => document.removeEventListener('keydown', handleKeyDown)
  }, [selection, ready, saving])

  async function closeWindow(options: CloseOptions = {}) {
    if (closingRef.current) return
    closingRef.current = true
    const id = captureIdRef.current || captureId
    const currentRequestId = requestIdRef.current
    await hideCaptureWindow()
    if (!options.keepHiddenMainWindow) {
      window.clearTimeout(restoreTimerRef.current)
      await invoke('restore_main_window').catch(() => undefined)
      hiddenMainWindowRef.current = false
    }
    await Promise.all([
      id ? invoke('release_screenshot_capture', { captureId: id }).catch(() => undefined) : Promise.resolve(),
      options.silent ? Promise.resolve() : emit('qqnt://screenshot/cancelled', { requestId: currentRequestId }).catch(() => undefined),
      invoke('set_screenshot_window_exclude_from_capture', { enable: true }).catch(() => undefined)
    ])
    captureIdRef.current = ''
    setCaptureId('')
    setReady(false)
    setSelection(null)
    setSaving(false)
    startedRef.current = false
    closingRef.current = false
    if (options.deferClose) return
    await closeCaptureWindow()
  }

  function clientPoint(event: React.PointerEvent<HTMLDivElement>): Point {
    const rect = event.currentTarget.getBoundingClientRect()
    return {
      x: Math.max(0, Math.min(rect.width, event.clientX - rect.left)),
      y: Math.max(0, Math.min(rect.height, event.clientY - rect.top))
    }
  }

  function handlePointerDown(event: React.PointerEvent<HTMLDivElement>) {
    if (!ready || saving || event.button !== 0) return
    event.currentTarget.setPointerCapture(event.pointerId)
    const point = clientPoint(event)
    dragStartRef.current = point
    setSelection({ x: point.x, y: point.y, width: 0, height: 0 })
  }

  function handlePointerMove(event: React.PointerEvent<HTMLDivElement>) {
    if (!dragStartRef.current || saving) return
    setSelection(normalizeSelection(dragStartRef.current, clientPoint(event)))
  }

  function handlePointerUp(event: React.PointerEvent<HTMLDivElement>) {
    if (!dragStartRef.current) return
    setSelection(normalizeSelection(dragStartRef.current, clientPoint(event)))
    dragStartRef.current = null
    event.currentTarget.releasePointerCapture(event.pointerId)
  }

  async function confirmSelection() {
    if (!selection || !ready || saving) return
    const canvas = canvasRef.current
    if (!canvas || selection.width < 4 || selection.height < 4) return

    const scaleX = canvas.width / canvas.clientWidth
    const scaleY = canvas.height / canvas.clientHeight
    const payload = {
      captureId: captureIdRef.current || captureId,
      x: Math.round(selection.x * scaleX),
      y: Math.round(selection.y * scaleY),
      width: Math.round(selection.width * scaleX),
      height: Math.round(selection.height * scaleY)
    }

    setSaving(true)
    try {
      const cropped = await invoke<ScreenshotResponse>('crop_screenshot', { payload })
      const filePath = responsePath(cropped)
      if (!filePath) throw new Error('截图完成，但没有返回图片路径')
      window.clearTimeout(restoreTimerRef.current)
      await invoke('restore_main_window').catch(() => undefined)
      hiddenMainWindowRef.current = false
      await emit('qqnt://screenshot/captured', {
        requestId: requestIdRef.current,
        filePath,
        fileName: responseName(cropped)
      })
      void closeWindow({ silent: true, deferClose: true })
    } catch (err) {
      setSaving(false)
      setError(err instanceof Error ? err.message : '截图保存失败')
    }
  }

  const canConfirm = Boolean(selection && selection.width >= 4 && selection.height >= 4 && !saving)
  const viewportStyle = error
    ? undefined
    : {
        width: viewportSize.width,
        height: viewportSize.height
      }

  return (
    <main
      className="relative h-screen w-screen select-none overflow-hidden bg-transparent text-white"
      style={viewportStyle}
    >
      <canvas
        ref={canvasRef}
        className={`absolute left-0 top-0 ${ready ? 'block' : 'hidden'}`}
        style={{
          width: viewportSize.width,
          height: viewportSize.height
        }}
      />

      {ready ? (
        <div
          className="absolute inset-0 cursor-crosshair"
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onPointerCancel={handlePointerUp}
        >
          {selection ? (
          <>
            <div
              className="absolute border border-[#12b7ff] bg-[#12b7ff]/10 shadow-[0_0_0_9999px_rgba(0,0,0,0.42)]"
              style={{
                left: selection.x,
                top: selection.y,
                width: selection.width,
                height: selection.height
              }}
            />
            <div
              className="absolute flex items-center gap-2 rounded-lg bg-[#151515]/92 p-1.5 text-xs shadow-2xl"
              style={{
                left: Math.min(selection.x + selection.width - 118, viewportSize.width - 128),
                top: Math.min(selection.y + selection.height + 8, viewportSize.height - 42)
              }}
              onPointerDown={(event) => event.stopPropagation()}
            >
              <button
                type="button"
                onClick={() => void closeWindow()}
                className="inline-flex h-8 items-center gap-1 rounded-md px-2 hover:bg-white/12"
              >
                <X size={15} />
                取消
              </button>
              <button
                type="button"
                onClick={confirmSelection}
                disabled={!canConfirm}
                className="inline-flex h-8 items-center gap-1 rounded-md bg-[#12b7ff] px-2 font-medium text-white disabled:opacity-50"
              >
                <Check size={15} />
                完成
              </button>
            </div>
          </>
          ) : null}
        </div>
      ) : null}

      {error ? <span className="sr-only">{error}</span> : null}
    </main>
  )
}


