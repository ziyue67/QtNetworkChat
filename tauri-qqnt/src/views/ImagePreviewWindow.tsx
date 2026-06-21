import { useEffect, useRef, useState } from 'react'
import { useSearchParams } from 'react-router-dom'
import { invoke } from '@tauri-apps/api/core'
import { LogicalSize } from '@tauri-apps/api/dpi'
import { currentMonitor, getCurrentWindow } from '@tauri-apps/api/window'
import { RotateCcw, ZoomIn, ZoomOut } from 'lucide-react'

interface ImageBase64Response {
  dataUrl: string
}

interface Point {
  x: number
  y: number
}

interface DragState {
  pointerId: number
  start: Point
  origin: Point
}

interface PreviewLayout {
  width: number
  height: number
}

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value))
}

const MIN_WINDOW_WIDTH = 420
const MIN_WINDOW_HEIGHT = 320
const IMAGE_PADDING = 24
const TOOLBAR_HEIGHT = 56
const WINDOW_VERTICAL_EXTRA = TOOLBAR_HEIGHT + IMAGE_PADDING
const MAX_SCREEN_RATIO = 0.92

async function fitPreviewWindowToImage(imageWidth: number, imageHeight: number) {
  if (!imageWidth || !imageHeight) return null

  const previewWindow = getCurrentWindow()
  const monitor = await currentMonitor().catch(() => null)
  const scaleFactor = monitor?.scaleFactor || (await previewWindow.scaleFactor().catch(() => 1)) || 1
  const workAreaWidth = monitor?.workArea.size.width ? monitor.workArea.size.width / scaleFactor : 1200
  const workAreaHeight = monitor?.workArea.size.height ? monitor.workArea.size.height / scaleFactor : 850
  const maxWindowWidth = Math.max(MIN_WINDOW_WIDTH, workAreaWidth * MAX_SCREEN_RATIO)
  const maxWindowHeight = Math.max(MIN_WINDOW_HEIGHT, workAreaHeight * MAX_SCREEN_RATIO)
  const maxImageWidth = maxWindowWidth - IMAGE_PADDING
  const maxImageHeight = maxWindowHeight - WINDOW_VERTICAL_EXTRA
  const fitRatio = Math.min(maxImageWidth / imageWidth, maxImageHeight / imageHeight)
  const fittedImageWidth = imageWidth * fitRatio
  const fittedImageHeight = imageHeight * fitRatio
  const width = Math.round(clamp(fittedImageWidth + IMAGE_PADDING, MIN_WINDOW_WIDTH, maxWindowWidth))
  const height = Math.round(clamp(fittedImageHeight + WINDOW_VERTICAL_EXTRA, MIN_WINDOW_HEIGHT, maxWindowHeight))

  await previewWindow.setSize(new LogicalSize(width, height)).catch(() => undefined)
  await previewWindow.center().catch(() => undefined)

  return {
    width: Math.max(1, Math.round(width - IMAGE_PADDING)),
    height: Math.max(1, Math.round(height - WINDOW_VERTICAL_EXTRA))
  }
}

export function ImagePreviewWindow() {
  const [searchParams] = useSearchParams()
  const filePath = searchParams.get('path') || ''
  const [src, setSrc] = useState('')
  const [error, setError] = useState('')
  const [scale, setScale] = useState(1)
  const [offset, setOffset] = useState<Point>({ x: 0, y: 0 })
  const [layout, setLayout] = useState<PreviewLayout | null>(null)
  const dragRef = useRef<DragState | null>(null)

  useEffect(() => {
    let cancelled = false
    setError('')
    setSrc('')
    setLayout(null)

    if (!filePath) {
      setError('图片路径为空')
      return
    }

    invoke<ImageBase64Response>('read_image_base64', { filePath })
      .then((image) => {
        if (!cancelled) setSrc(image.dataUrl)
      })
      .catch((err) => {
        if (!cancelled) setError(err instanceof Error ? err.message : '图片打开失败')
      })

    return () => {
      cancelled = true
    }
  }, [filePath])

  function zoomBy(delta: number) {
    setScale((value) => clamp(Number((value + delta).toFixed(2)), 0.2, 6))
  }

  function resetView() {
    setScale(1)
    setOffset({ x: 0, y: 0 })
  }

  function handlePointerDown(event: React.PointerEvent<HTMLDivElement>) {
    if (event.button !== 0 || !src) return
    event.currentTarget.setPointerCapture(event.pointerId)
    dragRef.current = {
      pointerId: event.pointerId,
      start: { x: event.clientX, y: event.clientY },
      origin: offset
    }
  }

  function handlePointerMove(event: React.PointerEvent<HTMLDivElement>) {
    const drag = dragRef.current
    if (!drag || drag.pointerId !== event.pointerId) return
    setOffset({
      x: drag.origin.x + event.clientX - drag.start.x,
      y: drag.origin.y + event.clientY - drag.start.y
    })
  }

  function handlePointerUp(event: React.PointerEvent<HTMLDivElement>) {
    if (dragRef.current?.pointerId !== event.pointerId) return
    dragRef.current = null
    event.currentTarget.releasePointerCapture(event.pointerId)
  }

  function handleWheel(event: React.WheelEvent<HTMLDivElement>) {
    event.preventDefault()
    zoomBy(event.deltaY < 0 ? 0.1 : -0.1)
  }

  return (
    <main className="flex h-full w-full flex-col bg-[#1b1b1b] text-white">
      <div
        className="relative min-h-0 flex-1 overflow-hidden"
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onWheel={handleWheel}
      >
        {src ? (
          <img
            src={src}
            alt="图片预览"
            draggable={false}
            onLoad={(event) => {
              resetView()
              void fitPreviewWindowToImage(event.currentTarget.naturalWidth, event.currentTarget.naturalHeight).then(
                (nextLayout) => {
                  if (nextLayout) setLayout(nextLayout)
                }
              )
            }}
            className="absolute left-1/2 top-1/2 max-h-[calc(100%-24px)] max-w-[calc(100%-24px)] select-none object-contain"
            style={{
              transform: `translate(calc(-50% + ${offset.x}px), calc(-50% + ${offset.y}px)) scale(${scale})`,
              cursor: dragRef.current ? 'grabbing' : 'grab',
              width: layout ? `${layout.width}px` : undefined,
              height: layout ? `${layout.height}px` : undefined
            }}
          />
        ) : (
          <div className="flex h-full items-center justify-center text-sm text-white/70">
            {error || '图片加载中'}
          </div>
        )}
      </div>

      <div className="flex h-14 items-center justify-center gap-2 border-t border-white/10 bg-[#111] px-4">
        <button
          type="button"
          className="flex h-9 w-9 items-center justify-center rounded hover:bg-white/10"
          aria-label="缩小"
          onClick={() => zoomBy(-0.1)}
        >
          <ZoomOut size={18} />
        </button>
        <span className="w-14 text-center text-sm font-semibold">{Math.round(scale * 100)}%</span>
        <button
          type="button"
          className="flex h-9 w-9 items-center justify-center rounded hover:bg-white/10"
          aria-label="放大"
          onClick={() => zoomBy(0.1)}
        >
          <ZoomIn size={18} />
        </button>
        <div className="mx-2 h-5 w-px bg-white/15" />
        <button
          type="button"
          className="flex h-9 items-center gap-2 rounded px-3 text-sm hover:bg-white/10"
          onClick={resetView}
        >
          <RotateCcw size={16} />
          <span>重置</span>
        </button>
      </div>
    </main>
  )
}
