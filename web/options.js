// Turns the option controls into an argv, and back into a readable command.
//
// The panel is a shortcut for the command line, never a replacement: whatever
// it produces is shown verbatim so the flags stay learnable and anything the
// panel does not cover can be typed into "Additional flags".

import { findStage } from './stages.js';

export const INPUT_FILE = 'design.sv';

export const DEFAULTS = {
  stage: 'run',
  std: '1800-2023',
  opt: '-O3',
  tier: 'native',
  scheduler: 'auto',
  specialization: 'auto',
  top: '',
  timescale: '',
  defines: '',
  extra: '',
};

/**
 * Split a flag string the way a shell would. Quotes may open anywhere in a
 * token, not just at its start, so `-DNAME="a b"` stays one argument; the
 * quotes themselves are removed because these go straight into argv with no
 * shell in between.
 */
export function splitFlags(text) {
  const out = [];
  let current = '';
  let quote = null;
  let open = false;

  for (const character of text) {
    if (quote) {
      if (character === quote) quote = null;
      else current += character;
      continue;
    }
    if (character === '"' || character === "'") {
      quote = character;
      open = true;
      continue;
    }
    if (/\s/.test(character)) {
      if (open) { out.push(current); current = ''; open = false; }
      continue;
    }
    current += character;
    open = true;
  }
  if (open) out.push(current);
  return out;
}

/** Re-quote for display only, so the preview stays copy-pasteable. */
function quoteForDisplay(argument) {
  return /\s/.test(argument) ? `'${argument}'` : argument;
}

/**
 * Build the compiler arguments, excluding `-o <path>` and the input file,
 * which the worker supplies.
 */
export function buildArgs(options) {
  const stage = findStage(options.stage);
  const args = [];

  if (stage.flag) args.push(stage.flag);
  args.push(`--std=${options.std}`);
  args.push(options.opt);

  // The driver rejects these unless it is producing a native executable, so
  // only emit them for the Run stage rather than letting it fail.
  if (stage.kind === 'binary') {
    if (options.tier !== DEFAULTS.tier) args.push(`--execution-tier=${options.tier}`);
    if (options.scheduler !== DEFAULTS.scheduler) args.push(`--native-scheduler=${options.scheduler}`);
  }
  if (options.specialization !== DEFAULTS.specialization) {
    args.push(`--static-specialization=${options.specialization}`);
  }

  if (options.top.trim()) args.push(`--top=${options.top.trim()}`);
  if (options.timescale.trim()) args.push(`--timescale=${options.timescale.trim()}`);

  for (const macro of splitFlags(options.defines)) args.push(`-D${macro}`);
  args.push(...splitFlags(options.extra));

  return args;
}

/** The command as a user would type it, for display only. */
export function formatCommand(options) {
  return ['obelisk', ...buildArgs(options).map(quoteForDisplay), INPUT_FILE].join(' ');
}

/* ------------------------------------------------------------ persistence */

const STORAGE_KEY = 'obelisk.playground';

// btoa is byte-oriented, so round-trip through UTF-8 explicitly.
function encode(value) {
  const json = JSON.stringify(value);
  const bytes = new TextEncoder().encode(json);
  let binary = '';
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

function decode(text) {
  const padded = text.replace(/-/g, '+').replace(/_/g, '/');
  const binary = atob(padded + '='.repeat((4 - (padded.length % 4)) % 4));
  const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
  return JSON.parse(new TextDecoder().decode(bytes));
}

export function toPermalink(source, options) {
  const url = new URL(window.location.href);
  url.hash = encode({ v: 1, source, options });
  return url.toString();
}

/** A permalink wins over saved state, so shared links open as sent. */
export function loadState() {
  const hash = window.location.hash.slice(1);
  if (hash) {
    try {
      const state = decode(hash);
      if (state?.source !== undefined) {
        return { source: state.source, options: { ...DEFAULTS, ...state.options } };
      }
    } catch {
      // Fall through to saved state rather than failing to open.
    }
  }
  try {
    const saved = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? 'null');
    if (saved?.source !== undefined) {
      return { source: saved.source, options: { ...DEFAULTS, ...saved.options } };
    }
  } catch {
    // Ignore unreadable storage; defaults are always valid.
  }
  return null;
}

export function saveState(source, options) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify({ source, options }));
  } catch {
    // Private browsing or a full quota: not worth interrupting the user.
  }
}
