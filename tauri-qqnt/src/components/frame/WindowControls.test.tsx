import { describe, expect, it } from 'vitest'
import { render, screen } from '@testing-library/react'
import { WindowControls } from './WindowControls'

describe('WindowControls', () => {
  it('shows only close on login/register screens', () => {
    render(<WindowControls variant="close-only" />)

    expect(screen.queryByLabelText('最小化')).toBeNull()
    expect(screen.queryByLabelText('最大化')).toBeNull()
    expect(screen.getByLabelText('关闭')).toBeTruthy()
  })

  it('shows minimize, maximize and close on main screens', () => {
    render(<WindowControls variant="full" />)

    expect(screen.getByLabelText('最小化')).toBeTruthy()
    expect(screen.getByLabelText('最大化')).toBeTruthy()
    expect(screen.getByLabelText('关闭')).toBeTruthy()
  })
})
