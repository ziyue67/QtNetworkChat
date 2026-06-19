import { useEffect, useState } from 'react'
import { invoke } from '@tauri-apps/api/core'
import { open } from '@tauri-apps/plugin-dialog'
import { ProfileCard } from '@/components/profile/ProfileCard'
import { useAuthStore } from '@/stores/authStore'
import { profileUpdate } from '@/api/qqnt'

interface ImageBase64Response {
  base64: string
  dataUrl: string
}

function avatarPayload(value: string) {
  if (!value) return ''
  if (value.startsWith('data:')) return value.split(',', 2)[1] || ''
  if (value.startsWith('http://') || value.startsWith('https://')) return ''
  return value
}

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
      const ack = await profileUpdate({ userName: nickname, avatarBase64: avatarPayload(avatar) })
      if (ack.status === 'error') throw new Error(ack.error?.message || '资料同步失败')
      setSaved(true)
    } catch (err) {
      setError(err instanceof Error ? err.message : '资料已保存到本地，等待引擎同步')
      setSaved(true)
    }
    window.setTimeout(() => setSaved(false), 2000)
  }

  async function handleAvatarClick() {
    setError('')
    try {
      const selected = await open({
        multiple: false,
        directory: false,
        title: '选择头像图片',
        filters: [
          {
            name: '图片',
            extensions: ['png', 'jpg', 'jpeg', 'gif', 'webp', 'bmp', 'svg', 'avif', 'apng']
          }
        ]
      })

      if (!selected || Array.isArray(selected)) return
      const image = await invoke<ImageBase64Response>('read_image_base64', { filePath: selected })
      setAvatar(image.dataUrl)
    } catch (err) {
      setError(err instanceof Error ? err.message : '无法读取头像图片，请重新选择')
    }
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
        onAvatarPick={handleAvatarClick}
        onSave={handleSave}
      />
    </div>
  )
}
