import { describe, expect, it, vi } from 'vitest'
import { render, screen } from '@testing-library/react'
import { SettingsSidebar } from './SettingsView'

describe('SettingsView', () => {
  it('does not expose network host or port configuration in the client UI', () => {
    render(<SettingsSidebar active="general" onChange={vi.fn()} />)

    expect(screen.queryByText('网络')).toBeNull()
    expect(screen.queryByText('引擎地址')).toBeNull()
    expect(screen.queryByText('引擎端口')).toBeNull()
    expect(screen.queryByText('Redis')).toBeNull()
    expect(screen.queryByText('IP地址')).toBeNull()
  })
})
