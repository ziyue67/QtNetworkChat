import { describe, expect, it, vi } from 'vitest'
import { fireEvent, render, screen } from '@testing-library/react'
import { ProfileCard } from './ProfileCard'
import type { User } from '@/types/qqnt'

const user: User = {
  id: '10001',
  nickname: '小企鹅',
  status: 'online',
  signature: 'hello'
}

describe('ProfileCard', () => {
  it('renders account profile fields and dispatches edits without URL/Base64 inputs', () => {
    const onNicknameChange = vi.fn()
    const onSignatureChange = vi.fn()
    const onAvatarPick = vi.fn()
    const onSave = vi.fn()

    render(
      <ProfileCard
        currentUser={user}
        nickname="小企鹅"
        signature="hello"
        avatar=""
        saved={false}
        error=""
        onNicknameChange={onNicknameChange}
        onSignatureChange={onSignatureChange}
        onAvatarPick={onAvatarPick}
        onSave={onSave}
      />
    )

    expect(screen.getByText('个人资料')).toBeTruthy()
    expect(screen.getByText('10001')).toBeTruthy()
    expect(screen.queryByLabelText('头像 URL / Base64')).toBeNull()

    fireEvent.change(screen.getByLabelText('昵称'), { target: { value: '新昵称' } })
    fireEvent.change(screen.getByLabelText('个性签名'), { target: { value: 'new signature' } })
    fireEvent.click(screen.getByLabelText('修改头像'))
    fireEvent.click(screen.getByRole('button', { name: '保存' }))

    expect(onNicknameChange).toHaveBeenCalledWith('新昵称')
    expect(onSignatureChange).toHaveBeenCalledWith('new signature')
    expect(onAvatarPick).toHaveBeenCalledTimes(1)
    expect(onSave).toHaveBeenCalledTimes(1)
  })
})
