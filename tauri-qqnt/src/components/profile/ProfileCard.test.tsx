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
  it('renders account profile fields and dispatches edits', () => {
    const onNicknameChange = vi.fn()
    const onSignatureChange = vi.fn()
    const onAvatarChange = vi.fn()
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
        onAvatarChange={onAvatarChange}
        onAvatarPick={onAvatarPick}
        onSave={onSave}
      />
    )

    expect(screen.getByText('个人资料')).toBeTruthy()
    expect(screen.getByText('10001')).toBeTruthy()

    fireEvent.change(screen.getByLabelText('昵称'), { target: { value: '新昵称' } })
    fireEvent.change(screen.getByLabelText('个性签名'), { target: { value: 'new signature' } })
    fireEvent.change(screen.getByLabelText('头像 URL / Base64'), { target: { value: 'avatar-url' } })
    fireEvent.click(screen.getByLabelText('修改头像'))
    fireEvent.click(screen.getByRole('button', { name: '保存' }))

    expect(onNicknameChange).toHaveBeenCalledWith('新昵称')
    expect(onSignatureChange).toHaveBeenCalledWith('new signature')
    expect(onAvatarChange).toHaveBeenCalledWith('avatar-url')
    expect(onAvatarPick).toHaveBeenCalledTimes(1)
    expect(onSave).toHaveBeenCalledTimes(1)
  })
})
