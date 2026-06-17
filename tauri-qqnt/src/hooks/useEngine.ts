import { useEffect, useState } from 'react'
import type { EngineState } from '@/types/qqnt'

export function useEngine() {
  const [engine, setEngine] = useState<EngineState>({
    ready: false,
    connected: false,
    protocolVersion: '1.0.0'
  })

  useEffect(() => {
    // Phase 2 stub: simulate engine handshake after mount.
    const timer = setTimeout(() => {
      setEngine({
        ready: true,
        connected: false,
        protocolVersion: '1.0.0'
      })
    }, 600)
    return () => clearTimeout(timer)
  }, [])

  return { engine, setEngine }
}