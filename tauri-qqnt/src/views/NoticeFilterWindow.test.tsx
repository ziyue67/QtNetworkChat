import { describe, expect, it } from 'vitest'
import '@testing-library/jest-dom'
import { render, screen } from '@testing-library/react'
import { MemoryRouter, Route, Routes } from 'react-router-dom'
import { NoticeFilterWindow } from './NoticeFilterWindow'

describe('NoticeFilterWindow', () => {
  it('renders filtered friend notice empty state', () => {
    render(
      <MemoryRouter initialEntries={['/notice-filter?type=friend']}>
        <Routes>
          <Route path="/notice-filter" element={<NoticeFilterWindow />} />
        </Routes>
      </MemoryRouter>
    )

    expect(screen.getByRole('heading', { name: '已过滤的通知' })).toBeTruthy()
    expect(screen.getByText('暂无通知')).toBeTruthy()
    expect(screen.getByText('好友通知 中没有符合筛选条件的通知')).toBeTruthy()
  })

  it('renders filtered group notice empty state', () => {
    render(
      <MemoryRouter initialEntries={['/notice-filter?type=group']}>
        <Routes>
          <Route path="/notice-filter" element={<NoticeFilterWindow />} />
        </Routes>
      </MemoryRouter>
    )

    expect(screen.getByText('群通知 中没有符合筛选条件的通知')).toBeTruthy()
  })
})
