import { describe, expect, it } from 'vitest'
import { settingsToEnginePayload } from './qqnt'

describe('settingsToEnginePayload', () => {
  it('maps the UI download path to the engine download directory field', () => {
    expect(settingsToEnginePayload({ downloadPath: 'C:\\Users\\jun23\\Downloads' })).toMatchObject({
      downloadPath: 'C:\\Users\\jun23\\Downloads',
      fileDownloadDir: 'C:\\Users\\jun23\\Downloads'
    })
  })

  it('does not add an engine download directory when the UI path is blank', () => {
    expect(settingsToEnginePayload({ downloadPath: '   ' })).not.toHaveProperty('fileDownloadDir')
  })
})
