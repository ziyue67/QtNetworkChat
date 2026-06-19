import { useCallback, useEffect, useState } from 'react'
import { useSessionStore } from '@/stores/sessionStore'
import { useMessageStore } from '@/stores/messageStore'
import { useAuthStore } from '@/stores/authStore'
import { useContactStore } from '@/stores/contactStore'
import { SessionList } from '@/components/session/SessionList'
import { ChatPanel } from '@/components/chat/ChatPanel'
import { cancelTransfer, sendPrivateMessage, sendGroupMessage, sendFile, sendImage, newReqId } from '@/api/qqnt'
import { openPath, revealItemInDir } from '@tauri-apps/plugin-opener'
import type { Message, User } from '@/types/qqnt'
import type { MentionCandidate } from '@/components/chat/Composer'

const IMAGE_EXTENSIONS = new Set(['.apng', '.avif', '.bmp', '.gif', '.jpg', '.jpeg', '.png', '.svg', '.webp'])

function baseName(path: string) {
  return path.split(/[\\/]/).filter(Boolean).pop() || path
}

function extension(path: string) {
  const name = baseName(path)
  const index = name.lastIndexOf('.')
  return index >= 0 ? name.slice(index).toLowerCase() : ''
}

function isImagePath(path: string) {
  return IMAGE_EXTENSIONS.has(extension(path))
}

function normalizeDialogSelection(selection: string | string[] | null) {
  if (!selection) return []
  return Array.isArray(selection) ? selection : [selection]
}

function asMentionCandidate(user: Pick<User, 'id' | 'nickname' | 'avatar'>): MentionCandidate {
  return { id: user.id, nickname: user.nickname, avatar: user.avatar }
}

function uniqueMembers(members: MentionCandidate[]) {
  const seen = new Set<string>()
  return members.filter((member) => {
    if (!member.id || seen.has(member.id)) return false
    seen.add(member.id)
    return true
  })
}

function selectionError(error: unknown) {
  const message = error instanceof Error ? error.message : typeof error === 'string' ? error : ''
  if (!message) return '无法打开系统资源管理器，请确认当前是在 Tauri 客户端内运行。'
  return `无法打开系统资源管理器：${message}`
}

export function MessageView() {
  const [dragActive, setDragActive] = useState(false)
  const [attachmentError, setAttachmentError] = useState('')
  const sessions = useSessionStore((state) => state.sessions)
  const activeSessionId = useSessionStore((state) => state.activeSessionId)
  const setActiveSession = useSessionStore((state) => state.setActiveSession)
  const markRead = useSessionStore((state) => state.markRead)
  const updateSession = useSessionStore((state) => state.updateSession)
  const messages = useMessageStore((state) => state.messages)
  const addMessage = useMessageStore((state) => state.addMessage)
  const updateMessageStatus = useMessageStore((state) => state.updateMessageStatus)
  const currentUser = useAuthStore((state) => state.currentUser)
  const contacts = useContactStore((state) => state.contacts)
  const groups = useContactStore((state) => state.groups)

  const activeSession = sessions.find((session) => session.id === activeSessionId)
  const activeMessages = activeSessionId ? messages[activeSessionId] || [] : []
  const activeContact = activeSession ? [...contacts, ...groups].find((contact) => contact.id === activeSession.id) : undefined
  const activeMembers = uniqueMembers([
    ...(currentUser ? [asMentionCandidate(currentUser)] : []),
    ...(activeSession?.type === 'group' ? activeContact?.members?.map(asMentionCandidate) || [] : []),
    ...(activeSession?.type === 'private' && activeContact ? [asMentionCandidate(activeContact)] : []),
    ...activeMessages.map((message) => ({ id: message.senderId, nickname: message.senderName }))
  ])

  const handleFileDrop = useCallback(async (paths: string[]) => {
    if (!activeSession || !currentUser) return
    setAttachmentError('')

    for (const filePath of paths.filter(Boolean)) {
      const image = isImagePath(filePath)
      const clientId = `client-${newReqId()}`
      const fileName = baseName(filePath)
      const optimistic: Message = {
        id: clientId,
        sessionId: activeSession.id,
        senderId: currentUser.id,
        senderName: currentUser.nickname,
        type: image ? 'image' : 'file',
        content: fileName,
        timestamp: Date.now(),
        status: 'sending',
        fileInfo: {
          id: clientId,
          name: fileName,
          size: 0,
          mime: image ? 'image/*' : 'application/octet-stream',
          progress: 0,
          path: filePath
        }
      }

      addMessage(activeSession.id, optimistic)
      updateSession(activeSession.id, { lastMessage: image ? '[图片]' : `[文件] ${fileName}`, lastTime: optimistic.timestamp })

      try {
        const args = activeSession.type === 'group'
          ? { groupId: activeSession.id, filePath }
          : { receiverId: activeSession.id, filePath }
        const ack = image ? await sendImage(args) : await sendFile(args)
        const transferId = ack.payload?.transferId
        const accepted = ack.status === 'ok' && (ack.payload?.accepted ?? true)
        if (accepted) {
          useMessageStore.getState().updateFileMessage(clientId, transferId ? { id: transferId } : {}, 'sending')
        } else {
          updateMessageStatus(activeSession.id, clientId, 'failed')
        }
      } catch (error) {
        setAttachmentError(error instanceof Error ? error.message : '文件发送失败')
        updateMessageStatus(activeSession.id, clientId, 'failed')
      }
    }
  }, [activeSession, addMessage, currentUser, updateMessageStatus, updateSession])

  useEffect(() => {
    let unlisten: (() => void) | undefined
    let cancelled = false

    async function bindDrop() {
      try {
        const { getCurrentWebview } = await import('@tauri-apps/api/webview')
        const cleanup = await getCurrentWebview().onDragDropEvent((event) => {
          if (event.payload.type === 'enter' || event.payload.type === 'over') {
            setDragActive(true)
            return
          }
          setDragActive(false)
          if (event.payload.type === 'drop') {
            void handleFileDrop(event.payload.paths)
          }
        })
        if (cancelled) cleanup()
        else unlisten = cleanup
      } catch {
        // Not running inside Tauri.
      }
    }

    bindDrop()
    return () => {
      cancelled = true
      unlisten?.()
    }
  }, [handleFileDrop])

  const handleSelect = (id: string) => {
    setAttachmentError('')
    setActiveSession(id)
    markRead(id)
  }

  const deliver = async (content: string, clientId: string) => {
    if (!activeSession) return
    try {
      const ack =
        activeSession.type === 'group'
          ? await sendGroupMessage(activeSession.id, content)
          : await sendPrivateMessage(activeSession.id, content)
      updateMessageStatus(activeSession.id, clientId, ack.status === 'ok' ? 'sent' : 'failed')
    } catch {
      updateMessageStatus(activeSession.id, clientId, 'failed')
    }
  }

  const handleSend = async (content: string) => {
    if (!activeSession || !currentUser) return

    const clientId = `client-${newReqId()}`
    const optimistic: Message = {
      id: clientId,
      sessionId: activeSession.id,
      senderId: currentUser.id,
      senderName: currentUser.nickname,
      type: 'text',
      content,
      timestamp: Date.now(),
      status: 'sending'
    }

    addMessage(activeSession.id, optimistic)
    updateSession(activeSession.id, { lastMessage: content, lastTime: optimistic.timestamp })

    await deliver(content, clientId)
  }

  const handlePickFiles = async () => {
    try {
      setAttachmentError('')
      const { open } = await import('@tauri-apps/plugin-dialog')
      const selection = await open({ multiple: true, directory: false, title: '选择要发送的文件' })
      await handleFileDrop(normalizeDialogSelection(selection))
    } catch (error) {
      setAttachmentError(selectionError(error))
    }
  }

  const handlePickImages = async () => {
    try {
      setAttachmentError('')
      const { open } = await import('@tauri-apps/plugin-dialog')
      const selection = await open({
        multiple: true,
        directory: false,
        title: '选择要发送的图片',
        filters: [{ name: 'Images', extensions: Array.from(IMAGE_EXTENSIONS).map((item) => item.slice(1)) }]
      })
      await handleFileDrop(normalizeDialogSelection(selection))
    } catch (error) {
      setAttachmentError(selectionError(error))
    }
  }

  const handleRetry = async (clientId: string) => {
    const msg = activeMessages.find((m) => m.id === clientId)
    if (!msg || msg.status !== 'failed' || !activeSession) return
    updateMessageStatus(activeSession.id, clientId, 'sending')
    await deliver(msg.content, clientId)
  }

  const handleCancelFile = async (transferId: string) => {
    try {
      await cancelTransfer(transferId)
    } finally {
      useMessageStore.getState().updateFileMessage(transferId, {}, 'failed')
    }
  }

  const handleDownloadFile = async (message: Message) => {
    if (message.fileInfo?.path) await openPath(message.fileInfo.path)
  }

  const handleOpenFolder = async (message: Message) => {
    if (message.fileInfo?.path) await revealItemInDir(message.fileInfo.path)
  }

  return (
    <div className="flex h-full w-full bg-[var(--qq-bg)]">
      <SessionList sessions={sessions} activeSessionId={activeSessionId} onSelect={handleSelect} />
      {activeSession ? (
        <ChatPanel
          session={activeSession}
          messages={activeMessages}
          currentUser={currentUser}
          members={activeMembers}
          onSend={handleSend}
          onPickFiles={handlePickFiles}
          onPickImages={handlePickImages}
          onRetry={handleRetry}
          onCancelFile={handleCancelFile}
          onDownloadFile={handleDownloadFile}
          onOpenFolder={handleOpenFolder}
          dragActive={dragActive}
          attachmentError={attachmentError}
          peerStatus={activeContact?.status}
        />
      ) : (
        <main className="flex min-w-0 flex-1 items-center justify-center text-sm text-[var(--qq-text-secondary)]">
          选择一个会话开始聊天
        </main>
      )}
    </div>
  )
}
