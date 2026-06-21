import { useCallback, useEffect, useRef, useState } from 'react'
import { normalizeSession, normalizeSessionId, useSessionStore } from '@/stores/sessionStore'
import { useMessageStore } from '@/stores/messageStore'
import { useAuthStore } from '@/stores/authStore'
import { useContactStore } from '@/stores/contactStore'
import { useUIStore } from '@/stores/uiStore'
import { SessionList } from '@/components/session/SessionList'
import { ChatPanel } from '@/components/chat/ChatPanel'
import { onScreenshotShortcut, useScreenshotShortcut } from '@/hooks/useScreenshotShortcut'
import { formatShortcutLabel } from '@/lib/shortcut'
import {
  addLocalFriend,
  addLocalEmoji,
  cancelTransfer,
  blockLocalUser,
  clearSessionHistory,
  deleteLocalMessage,
  editLocalGroupNickname,
  favoriteLocalMessage,
  multiSelectLocalMessage,
  quoteLocalMessage,
  recallLocalMessage,
  reportLocalUser,
  sendPrivateMessage,
  sendGroupMessage,
  sendFile,
  sendImage,
  setEssenceLocalMessage,
  viewLocalProfile,
  newReqId
} from '@/api/qqnt'
import { invoke } from '@tauri-apps/api/core'
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

function normalizeMessagesBySession(messages: Record<string, Message[]>): Record<string, Message[]> {
  return Object.fromEntries(
    Object.entries(messages).map(([sessionId, sessionMessages]) => {
      const normalizedId = normalizeSessionId(sessionId)
      return [
        normalizedId,
        sessionMessages.map((message) => ({ ...message, sessionId: normalizeSessionId(message.sessionId) }))
      ]
    })
  )
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

type AttachmentMode = 'auto' | 'image'

interface SavedLocalFileResponse {
  filePath?: string
  fileName?: string
  file_path?: string
  file_name?: string
  x?: number
  y?: number
  width?: number
  height?: number
}

interface ScreenshotCapturedPayload {
  requestId?: string
  filePath?: string
  fileName?: string
}

interface ScreenshotWindowEventPayload {
  requestId?: string
  message?: string
}

const SCREENSHOT_WINDOW_READY_EVENT = 'qqnt://screenshot/window-ready'
const SCREENSHOT_START_EVENT = 'qqnt://screenshot/start'
const SCREENSHOT_FAILED_EVENT = 'qqnt://screenshot/failed'
const SCREENSHOT_WINDOW_OPTIONS = {
  url: 'index.html#/screenshot-capture',
  title: '截图',
  x: -32000,
  y: -32000,
  width: 1,
  height: 1,
  minWidth: 1,
  minHeight: 1,
  resizable: false,
  decorations: false,
  shadow: false,
  alwaysOnTop: true,
  skipTaskbar: true,
  transparent: true,
  visible: false,
  focus: false
} as const

function savedFilePath(saved?: SavedLocalFileResponse) {
  return saved?.filePath || saved?.file_path || ''
}

function savedFileName(saved?: SavedLocalFileResponse) {
  return saved?.fileName || saved?.file_name || ''
}

export function MessageView() {
  const [dragActive, setDragActive] = useState(false)
  const [attachmentError, setAttachmentError] = useState('')
  const [actionNotice, setActionNoticeState] = useState('')
  const [screenshotBusy, setScreenshotBusy] = useState(false)
  const screenshotInFlightRef = useRef(false)
  const sendLockRef = useRef<Record<string, number>>({})
  const rawSessions = useSessionStore((state) => state.sessions)
  const activeSessionId = useSessionStore((state) => state.activeSessionId)
  const setActiveSession = useSessionStore((state) => state.setActiveSession)
  const markRead = useSessionStore((state) => state.markRead)
  const updateSession = useSessionStore((state) => state.updateSession)
  const upsertSession = useSessionStore((state) => state.upsertSession)
  const rawMessages = useMessageStore((state) => state.messages)
  const addMessage = useMessageStore((state) => state.addMessage)
  const updateMessageStatus = useMessageStore((state) => state.updateMessageStatus)
  const removeMessage = useMessageStore((state) => state.removeMessage)
  const clearSessionMessages = useMessageStore((state) => state.clearSessionMessages)
  const currentUser = useAuthStore((state) => state.currentUser)
  const contacts = useContactStore((state) => state.contacts)
  const groups = useContactStore((state) => state.groups)
  const ensureDefaultDownloadPath = useUIStore((state) => state.ensureDefaultDownloadPath)
  const screenshotShortcut = useUIStore((state) => state.settings.screenshotShortcut)
  const hideWindowBeforeScreenshot = useUIStore((state) => state.settings.hideWindowBeforeScreenshot)
  const updateSettings = useUIStore((state) => state.updateSettings)

  const sessions = rawSessions.map(normalizeSession)
  const messages = normalizeMessagesBySession(rawMessages)
  const activeSession = sessions.find((session) => normalizeSessionId(session.id) === activeSessionId)
  const activeMessages = activeSessionId ? messages[activeSessionId] || [] : []
  const activeContact = activeSession ? [...contacts, ...groups].find((contact) => contact.id === activeSession.id) : undefined
  const activeMembers = uniqueMembers([
    ...(currentUser ? [asMentionCandidate(currentUser)] : []),
    ...(activeSession?.type === 'group' ? activeContact?.members?.map(asMentionCandidate) || [] : []),
    ...(activeSession?.type === 'private' && activeContact ? [asMentionCandidate(activeContact)] : []),
    ...activeMessages.map((message) => ({ id: message.senderId, nickname: message.senderName }))
  ])

  useScreenshotShortcut(Boolean(activeSession && currentUser), screenshotShortcut)

  const saveMessageFileToDownloads = useCallback(async (message: Message, fallbackPath?: string, updateId?: string) => {
    const sourcePath = message.fileInfo?.path || fallbackPath
    if (!sourcePath) return undefined
    const directoryPath = await ensureDefaultDownloadPath()
    const saved = await invoke<SavedLocalFileResponse>('save_file_to_directory', {
      sourcePath,
      directoryPath,
      fileName: message.fileInfo?.name || message.content
    })
    const filePath = savedFilePath(saved)
    const fileName = savedFileName(saved)
    if (!filePath) {
      throw new Error('文件已保存，但客户端没有返回本地路径。')
    }
    useMessageStore.getState().updateFileMessage(updateId || message.fileInfo?.id || message.id, {
      path: filePath,
      name: fileName || message.fileInfo?.name || message.content,
      progress: 100
    }, message.status === 'sending' ? 'sent' : message.status)
    return { filePath, fileName }
  }, [ensureDefaultDownloadPath])

  const handleFileDrop = useCallback(async (paths: string[], mode: AttachmentMode = 'auto') => {
    if (!activeSession || !currentUser) return
    setAttachmentError('')

    for (const filePath of paths.filter(Boolean)) {
      const image = mode === 'image' || isImagePath(filePath)
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
          useMessageStore.getState().updateFileMessage(clientId, transferId ? { id: transferId } : {})
          void saveMessageFileToDownloads({
            ...optimistic,
            status: 'sent',
            fileInfo: {
              ...optimistic.fileInfo!,
              id: transferId || optimistic.fileInfo!.id
            }
          }, filePath, clientId).catch((error) => {
            setAttachmentError(error instanceof Error ? error.message : '文件保存到下载目录失败')
          })
        } else {
          updateMessageStatus(activeSession.id, clientId, 'failed')
        }
      } catch (error) {
        setAttachmentError(error instanceof Error ? error.message : '文件发送失败')
        updateMessageStatus(activeSession.id, clientId, 'failed')
      }
    }
  }, [activeSession, addMessage, currentUser, saveMessageFileToDownloads, updateMessageStatus, updateSession])

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
    setActionNoticeState('')
    setActiveSession(id)
    markRead(id)
  }

  const deliver = async (content: string, clientId: string) => {
    if (!activeSession) return
    const normalizedSessionId = normalizeSessionId(activeSession.id)
    try {
      const ack =
        activeSession.type === 'group'
          ? await sendGroupMessage(normalizedSessionId, content, clientId)
          : await sendPrivateMessage(normalizedSessionId, content, clientId)
      updateMessageStatus(normalizedSessionId, clientId, ack.status === 'ok' ? 'sent' : 'failed')
    } catch {
      updateMessageStatus(normalizedSessionId, clientId, 'failed')
    }
  }

  const handleSend = async (content: string) => {
    if (!activeSession || !currentUser) return

    const normalizedSessionId = normalizeSessionId(activeSession.id)
    const normalizedContent = content.trim()
    if (!normalizedContent) return
    const lockKey = `${normalizedSessionId}:${normalizedContent}`
    const now = Date.now()
    if (sendLockRef.current[lockKey] && now - sendLockRef.current[lockKey] < 2000) return
    sendLockRef.current[lockKey] = now
    window.setTimeout(() => {
      if (sendLockRef.current[lockKey] === now) delete sendLockRef.current[lockKey]
    }, 2200)

    const clientId = `client-${newReqId()}`
    const optimistic: Message = {
      id: clientId,
      clientMessageId: clientId,
      sessionId: activeSession.id,
      senderId: currentUser.id,
      senderName: currentUser.nickname,
      type: 'text',
      content: normalizedContent,
      timestamp: Date.now(),
      status: 'sending'
    }

    addMessage(normalizedSessionId, { ...optimistic, sessionId: normalizedSessionId })
    updateSession(normalizedSessionId, { lastMessage: normalizedContent, lastTime: optimistic.timestamp })

    await deliver(normalizedContent, clientId)
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

  const handleScreenshot = useCallback(async () => {
    if (screenshotInFlightRef.current) return
    screenshotInFlightRef.current = true
    setScreenshotBusy(true)
    let screenshotWindow: { close: () => Promise<void>; once?: (event: string, handler: () => void) => Promise<() => void> } | undefined
    const unlistens: Array<() => void> = []
    const restoreHiddenMainWindow = async () => {
      if (!hideWindowBeforeScreenshot) return
      await invoke('restore_main_window').catch(() => undefined)
    }
    try {
      setAttachmentError('')
      const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
      const { emit, listen } = await import('@tauri-apps/api/event')
      const requestId = `screenshot-${Date.now()}-${Math.random().toString(16).slice(2)}`
      const screenshotWindowLabel = `screenshot-capture-${requestId}`

      const waitForScreenshotWindowReady = new Promise<void>((resolve, reject) => {
        let settled = false
        const cleanups: Array<() => void> = []
        const cleanupAll = () => cleanups.splice(0).forEach((cleanup) => cleanup())
        const timer = window.setTimeout(() => {
          if (settled) return
          settled = true
          cleanupAll()
          reject(new Error('截图窗口准备超时，请重试。'))
        }, 8000)

        listen<ScreenshotWindowEventPayload>(SCREENSHOT_WINDOW_READY_EVENT, (event) => {
          if (event.payload?.requestId !== requestId) return
          settled = true
          window.clearTimeout(timer)
          cleanupAll()
          resolve()
        })
          .then((cleanup) => {
            cleanups.push(cleanup)
          })
          .catch(reject)
      })

      const captured = new Promise<string>((resolve, reject) => {
        let settled = false
        const cleanups: Array<() => void> = []
        const cleanupAll = () => cleanups.splice(0).forEach((cleanup) => cleanup())
        const timer = window.setTimeout(() => {
          if (settled) return
          settled = true
          cleanupAll()
          reject(new Error('截图窗口已超时，请重试。'))
        }, 30000)

        listen<ScreenshotCapturedPayload>('qqnt://screenshot/captured', (event) => {
          if (event.payload?.requestId !== requestId) return
          const selectedPath = event.payload.filePath || ''
          settled = true
          window.clearTimeout(timer)
          cleanupAll()
          if (selectedPath) resolve(selectedPath)
          else reject(new Error('截图完成，但没有返回选区图片路径。'))
        })
          .then((cleanup) => {
            cleanups.push(cleanup)
          })
          .catch(reject)
        listen<ScreenshotCapturedPayload>('qqnt://screenshot/cancelled', (event) => {
          if (event.payload?.requestId !== requestId) return
          settled = true
          window.clearTimeout(timer)
          cleanupAll()
          reject(new Error('已取消截图。'))
        })
          .then((cleanup) => {
            cleanups.push(cleanup)
          })
          .catch(reject)
        listen<ScreenshotWindowEventPayload>(SCREENSHOT_FAILED_EVENT, (event) => {
          if (event.payload?.requestId !== requestId) return
          settled = true
          window.clearTimeout(timer)
          cleanupAll()
          reject(new Error(event.payload.message || '截图窗口启动失败。'))
        })
          .then((cleanup) => {
            cleanups.push(cleanup)
          })
          .catch(reject)
      })

      screenshotWindow = new WebviewWindow(screenshotWindowLabel, {
        ...SCREENSHOT_WINDOW_OPTIONS,
        url: `index.html#/screenshot-capture?request=${encodeURIComponent(requestId)}`
      })
      screenshotWindow.once?.('tauri://destroyed', () => {
        void restoreHiddenMainWindow()
      }).then((cleanup) => unlistens.push(cleanup)).catch(() => undefined)

      await waitForScreenshotWindowReady
      await emit(SCREENSHOT_START_EVENT, { requestId, hideMainWindow: hideWindowBeforeScreenshot }).catch(() => undefined)

      const selectedPath = await captured
      await restoreHiddenMainWindow()
      await handleFileDrop([selectedPath], 'image')
    } catch (error) {
      await restoreHiddenMainWindow()
      const message = error instanceof Error ? error.message : typeof error === 'string' ? error : '截图失败'
      if (message !== '已取消截图。') {
        setAttachmentError(`截图失败：${message}`)
      }
    } finally {
      unlistens.splice(0).forEach((cleanup) => cleanup())
      await restoreHiddenMainWindow()
      await screenshotWindow?.close().catch(() => undefined)
      screenshotInFlightRef.current = false
      setScreenshotBusy(false)
    }
  }, [handleFileDrop, hideWindowBeforeScreenshot])

  useEffect(() => {
    let cleanup: (() => void) | undefined
    let cancelled = false

    onScreenshotShortcut(handleScreenshot)
      .then((unlisten) => {
        if (cancelled) unlisten()
        else cleanup = unlisten
      })
      .catch(() => undefined)

    return () => {
      cancelled = true
      cleanup?.()
    }
  }, [handleScreenshot])

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
      await handleFileDrop(normalizeDialogSelection(selection), 'image')
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
    let savedPath = ''
    try {
      setAttachmentError('')
      const saved = await saveMessageFileToDownloads(message)
      savedPath = saved?.filePath || message.fileInfo?.path || ''
    } catch (error) {
      setAttachmentError(error instanceof Error ? error.message : '文件保存到下载目录失败')
      return
    }

    if (!savedPath) {
      setAttachmentError('文件还没有保存到本地。')
      return
    }

    try {
      await openPath(savedPath)
    } catch (error) {
      try {
        await revealItemInDir(savedPath)
      } catch {
        const message = error instanceof Error ? error.message : '系统无法自动打开该文件'
        setAttachmentError(`文件已保存，但无法自动打开：${message}`)
      }
    }
  }

  const handleOpenFolder = async (message: Message) => {
    if (message.fileInfo?.path) await revealItemInDir(message.fileInfo.path)
  }

  const handleCopyMessage = async (message: Message) => {
    const text = message.type === 'image' ? message.fileInfo?.path || message.content : message.content
    if (!text) {
      setActionNotice('这条消息暂无可复制内容。')
      return
    }
    try {
      await navigator.clipboard.writeText(text)
      setActionNotice('已复制。')
    } catch {
      setAttachmentError('复制失败：系统剪贴板不可用。')
    }
  }


  const handleForwardMessage = async (message: Message) => {
    try {
      setAttachmentError('')
      setActionNoticeState('')
      const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
      window.localStorage.setItem(
        'qqnt:forward-snapshot',
        JSON.stringify({
          message,
          contacts,
          groups,
          sessions
        })
      )
      new WebviewWindow(`forward-message-${Date.now()}`, {
        url: 'index.html#/forward',
        title: '转发',
        width: 760,
        height: 560,
        minWidth: 640,
        minHeight: 480,
        center: true,
        resizable: false,
        decorations: false,
        visible: true
      })
    } catch (error) {
      setAttachmentError(error instanceof Error ? `转发窗口打开失败：${error.message}` : '转发窗口打开失败')
    }
  }

  const handleFavoriteMessage = async (message: Message) => {
    try {
      await favoriteLocalMessage(message)
      useMessageStore.getState().patchMessage(normalizeSessionId(message.sessionId), message.id, { localFavorite: true })
      setActionNotice('已收藏到本地。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `收藏失败：${error.message}` : '收藏失败')
    }
  }

  const handleAddEmoji = async (message: Message) => {
    try {
      await addLocalEmoji(message)
      useMessageStore.getState().patchMessage(normalizeSessionId(message.sessionId), message.id, { localEmoji: true })
      setActionNotice('已添加到本地表情。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `添加表情失败：${error.message}` : '添加表情失败')
    }
  }

  const handleDeleteMessage = async (message: Message) => {
    const sessionId = normalizeSessionId(message.sessionId)
    try {
      await deleteLocalMessage(sessionId, message.id)
    } catch {
      // 本地 UI 先删除；后端本地动作失败不阻塞用户操作。
    }
    removeMessage(sessionId, message.id)
  }

  const handleClearSessionMessages = async (sessionId: string) => {
    const normalizedSessionId = normalizeSessionId(sessionId)
    try {
      await clearSessionHistory(normalizedSessionId)
    } catch {
      // 当前历史在前端 store 中，后端命令失败时仍执行真实本地清空。
    }
    clearSessionMessages(normalizedSessionId)
    updateSession(normalizedSessionId, { lastMessage: '', lastTime: undefined, unread: 0 })
  }

  const setActionNotice = (label: string) => {
    setAttachmentError('')
    setActionNoticeState(label)
    window.setTimeout(() => {
      setActionNoticeState((current) => (current === label ? '' : current))
    }, 2500)
  }

  const handleOpenDirectMessage = (member: MentionCandidate) => {
    if (!member.id || member.id === currentUser?.id) return
    upsertSession({
      id: member.id,
      type: 'private',
      name: member.nickname || member.id,
      avatar: member.avatar,
      unread: 0,
      pinned: false
    })
    setActiveSession(member.id)
  }

  const handleViewProfile = async (member: MentionCandidate) => {
    try {
      await viewLocalProfile(member)
      setActionNotice('未开放之后添加，已记录查看资料操作。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `查看资料失败：${error.message}` : '查看资料失败')
    }
  }

  const handleAddFriend = async (member: MentionCandidate) => {
    if (!member.id || member.id === currentUser?.id) return
    try {
      await addLocalFriend(member)
      setActionNotice('未开放之后添加，已记录本地添加好友操作。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `添加好友记录失败：${error.message}` : '添加好友记录失败')
    }
  }

  const handleEditGroupNickname = async (member: MentionCandidate) => {
    const nickname = window.prompt(`修改 ${member.nickname || member.id} 的群昵称`, member.nickname || '')
    if (!nickname?.trim()) return
    try {
      await editLocalGroupNickname(member, nickname.trim(), activeSessionId || undefined)
      if (activeSessionId) {
        useMessageStore.getState().renameMemberMessages(activeSessionId, member.id, nickname.trim())
      }
      setActionNotice('已保存本地群昵称，后续接真实群资料接口。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `修改群昵称失败：${error.message}` : '修改群昵称失败')
    }
  }

  const handleReportUser = async (member: MentionCandidate) => {
    try {
      await reportLocalUser(member, activeSessionId || undefined)
      setActionNotice('未开放之后添加，已记录本地操作。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `举报失败：${error.message}` : '举报失败')
    }
  }

  const handleBlockUser = async (member: MentionCandidate) => {
    try {
      await blockLocalUser(member, activeSessionId || undefined)
      if (activeSessionId) {
        useMessageStore.getState().blockMemberMessages(activeSessionId, member.id)
      }
      setActionNotice('已本地屏蔽此人发言，后续接真实屏蔽接口。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `屏蔽失败：${error.message}` : '屏蔽失败')
    }
  }

  const handleMultiSelectMessage = async (message: Message) => {
    try {
      await multiSelectLocalMessage(message)
      useMessageStore.getState().patchMessage(normalizeSessionId(message.sessionId), message.id, { localSelected: true })
      setActionNotice('已进入多选模式，并记录当前选中消息。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `多选失败：${error.message}` : '多选失败')
    }
  }

  const handleQuoteMessage = async (message: Message) => {
    try {
      await quoteLocalMessage(message)
      useMessageStore.getState().patchMessage(normalizeSessionId(message.sessionId), message.id, {
        localQuote: message.content || message.fileInfo?.name || message.id
      })
      setActionNotice(`已引用：${message.content || message.fileInfo?.name || message.id}`)
    } catch (error) {
      setAttachmentError(error instanceof Error ? `引用失败：${error.message}` : '引用失败')
    }
  }

  const handleSetEssenceMessage = async (message: Message) => {
    try {
      await setEssenceLocalMessage(message)
      useMessageStore.getState().patchMessage(normalizeSessionId(message.sessionId), message.id, { localEssence: true })
      setActionNotice('已设为本地精华消息。')
    } catch (error) {
      setAttachmentError(error instanceof Error ? `设为精华失败：${error.message}` : '设为精华失败')
    }
  }

  const handleRecallMessage = async (message: Message) => {
    const sessionId = normalizeSessionId(message.sessionId)
    try {
      await recallLocalMessage(message)
    } catch {
      // 撤回先做本地可见效果，本地记录失败不阻塞 UI。
    }
    removeMessage(sessionId, message.id)
    setActionNotice('已撤回本地消息。')
  }

  const handlePreviewImage = async (message: Message) => {
    setAttachmentError('')
    const filePath = message.fileInfo?.path
    if (!filePath) {
      setAttachmentError('图片还没有保存到本地。')
      return
    }

    try {
      const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
      const label = `image-preview-${message.fileInfo?.id || message.id}-${Date.now()}`
      const url = `index.html#/image-preview?path=${encodeURIComponent(filePath)}`
      new WebviewWindow(label, {
        url,
        title: '图片预览',
        width: 620,
        height: 760,
        minWidth: 360,
        minHeight: 420,
        center: true,
        resizable: true,
        decorations: true,
        visible: true
      })
    } catch (error) {
      try {
        await openPath(filePath)
      } catch {
        setAttachmentError(error instanceof Error ? error.message : '图片预览窗口打开失败')
      }
    }
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
          onScreenshot={handleScreenshot}
          screenshotBusy={screenshotBusy}
          screenshotShortcutLabel={formatShortcutLabel(screenshotShortcut)}
          hideWindowBeforeScreenshot={hideWindowBeforeScreenshot}
          onHideWindowBeforeScreenshotChange={(checked) => updateSettings({ hideWindowBeforeScreenshot: checked })}
          onRetry={handleRetry}
          onCancelFile={handleCancelFile}
          onDownloadFile={handleDownloadFile}
          onOpenFolder={handleOpenFolder}
          onPreviewImage={handlePreviewImage}
          onCopyMessage={handleCopyMessage}
          onForwardMessage={handleForwardMessage}
          onFavoriteMessage={handleFavoriteMessage}
          onMultiSelectMessage={handleMultiSelectMessage}
          onQuoteMessage={handleQuoteMessage}
          onSetEssenceMessage={handleSetEssenceMessage}
          onRecallMessage={handleRecallMessage}
          onDeleteMessage={handleDeleteMessage}
          onAddEmoji={handleAddEmoji}
          onOpenDirectMessage={handleOpenDirectMessage}
          onViewProfile={handleViewProfile}
          onAddFriend={handleAddFriend}
          onEditGroupNickname={handleEditGroupNickname}
          onReportUser={handleReportUser}
          onBlockUser={handleBlockUser}
          onClearSessionMessages={handleClearSessionMessages}
          dragActive={dragActive}
          attachmentError={attachmentError}
          actionNotice={actionNotice}
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



