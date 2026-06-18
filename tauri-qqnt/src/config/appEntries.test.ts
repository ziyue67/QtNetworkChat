import { describe, expect, it } from 'vitest'
import { APP_ENTRIES } from './appEntries'

describe('APP_ENTRIES', () => {
  it('defines the 11 QQ NT navigation entries', () => {
    expect(APP_ENTRIES).toHaveLength(11)
    expect(APP_ENTRIES.map((entry) => entry.id)).toEqual([
      'messages',
      'contacts',
      'space',
      'channel',
      'mail',
      'docs',
      'calendar',
      'meeting',
      'favorites',
      'wallet',
      'settings'
    ])
    expect(APP_ENTRIES.filter((entry) => entry.mock).map((entry) => entry.id)).toEqual([
      'space',
      'channel',
      'mail',
      'docs',
      'calendar',
      'meeting',
      'favorites',
      'wallet'
    ])
  })
})
