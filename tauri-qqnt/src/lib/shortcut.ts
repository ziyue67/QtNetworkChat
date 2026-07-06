export const DEFAULT_SCREENSHOT_SHORTCUT = 'CommandOrControl+Alt+A'

const MODIFIER_LABELS: Record<string, string> = {
  CommandOrControl: 'Ctrl',
  CmdOrCtrl: 'Ctrl',
  Control: 'Ctrl',
  Ctrl: 'Ctrl',
  Alt: 'Alt',
  Option: 'Alt',
  Shift: 'Shift',
  Meta: 'Win',
  Command: 'Win',
  Super: 'Win'
}

const KEY_LABELS: Record<string, string> = {
  Space: 'Space',
  Plus: '+',
  Minus: '-',
  Equal: '=',
  Comma: ',',
  Period: '.',
  Slash: '/',
  Backslash: '\\',
  Semicolon: ';',
  Quote: "'",
  Backquote: '`',
  LeftBracket: '[',
  RightBracket: ']',
  Up: 'Up',
  Down: 'Down',
  Left: 'Left',
  Right: 'Right'
}

const KEY_ALIASES: Record<string, string> = {
  ' ': 'Space',
  Spacebar: 'Space',
  Escape: 'Esc',
  ArrowUp: 'Up',
  ArrowDown: 'Down',
  ArrowLeft: 'Left',
  ArrowRight: 'Right',
  '+': 'Plus',
  '-': 'Minus',
  '=': 'Equal',
  ',': 'Comma',
  '.': 'Period',
  '/': 'Slash',
  '\\': 'Backslash',
  ';': 'Semicolon',
  "'": 'Quote',
  '`': 'Backquote',
  '[': 'LeftBracket',
  ']': 'RightBracket'
}

const MODIFIER_KEYS = new Set(['Control', 'Shift', 'Alt', 'Meta', 'OS', 'Command'])

export function normalizeShortcut(shortcut?: string) {
  const value = shortcut?.trim()
  return value || DEFAULT_SCREENSHOT_SHORTCUT
}

export function formatShortcutLabel(shortcut?: string) {
  return normalizeShortcut(shortcut)
    .split('+')
    .filter(Boolean)
    .map((part) => MODIFIER_LABELS[part] || KEY_LABELS[part] || part)
    .join('+')
}

export function shortcutFromKeyboardEvent(event: Pick<KeyboardEvent, 'key' | 'ctrlKey' | 'metaKey' | 'altKey' | 'shiftKey'>) {
  if (MODIFIER_KEYS.has(event.key)) return ''

  const parts: string[] = []
  if (event.ctrlKey || event.metaKey) parts.push('CommandOrControl')
  if (event.altKey) parts.push('Alt')
  if (event.shiftKey) parts.push('Shift')

  let key = KEY_ALIASES[event.key] || event.key
  if (key.length === 1) key = key.toUpperCase()
  if (!key || MODIFIER_KEYS.has(key)) return ''

  parts.push(key)
  return parts.join('+')
}
