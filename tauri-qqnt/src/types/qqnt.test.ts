import { describe, expect, it } from 'vitest'
import { EXPECTED_PROTOCOL_VERSION, type QQNTCommandOp } from './qqnt'

const REQUIRED_COMMANDS = [
  'ready',
  'connect',
  'disconnect',
  'login',
  'register',
  'logout',
  'set_user_info',
  'get_user_list',
  'get_friend_list',
  'get_group_list',
  'search_friend',
  'send_friend_request',
  'respond_friend_request',
  'send_private_message',
  'send_group_message',
  'create_group',
  'update_group_announcement',
  'update_group_member',
  'send_file',
  'send_image',
  'cancel_transfer',
  'query_resume',
  'e2e_status',
  'e2e_announce_identity',
  'e2e_pin_identity',
  'e2e_request_rotation',
  'profile_update',
  'settings_sync'
] as const satisfies readonly QQNTCommandOp[]

describe('QQ NT protocol contract', () => {
  it('pins the frontend protocol version and command op list', () => {
    expect(EXPECTED_PROTOCOL_VERSION).toBe(1)
    expect(REQUIRED_COMMANDS).toHaveLength(28)
  })
})
