import { useEffect, useState } from 'react'
import { ProfileCard } from '@/components/profile/ProfileCard'
import { useAuthStore } from '@/stores/authStore'
import { profileUpdate } from '@/api/qqnt'

export function ProfileView() {
  const currentUser = useAuthStore((state) => state.currentUser)
  const setCurrentUser = useAuthStore((state) => state.setCurrentUser)
  const [nickname, setNickname] = useState(currentUser?.nickname || '')
  const [signature, setSignature] = useState(currentUser?.signature || '')
  const [avatar, setAvatar] = useState(currentUser?.avatar || '')
  const [saved, setSaved] = useState(false)
  const [error, setError] = useState('')

  useEffect(() => {
    setNickname(currentUser?.nickname || '')
    setSignature(currentUser?.signature || '')
    setAvatar(currentUser?.avatar || '')
  }, [currentUser])

  async function handleSave() {
    if (!currentUser) return
    setError('')
    const nextUser = { ...currentUser, nickname, signature, avatar }
    setCurrentUser(nextUser)
    try {
      const ack = await profileUpdate({ userName: nickname, signature, avatarBase64: avatar })
      if (ack.status === 'error') throw new Error(ack.error?.message || '资料同步失败')
      setSaved(true)
    } catch (err) {
      setError(err instanceof Error ? err.message : '资料已保存到本地，等待引擎同步')
      setSaved(true)
    }
    window.setTimeout(() => setSaved(false), 2000)
  }

  function handleAvatarClick() {
    const nextAvatar = window.prompt('头像 URL 或 Base64', avatar)
    if (nextAvatar !== null) setAvatar(nextAvatar)
  }

  return (
    <div className="flex h-full w-full items-center justify-center bg-[var(--qq-bg)] p-6">
      <ProfileCard
        currentUser={currentUser}
        nickname={nickname}
        signature={signature}
        avatar={avatar}
        saved={saved}
        error={error}
        onNicknameChange={setNickname}
        onSignatureChange={setSignature}
        onAvatarChange={setAvatar}
        onAvatarPick={handleAvatarClick}
        onSave={handleSave}
      />
    </div>
  )
}
