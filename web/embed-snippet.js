// Snippets that put the current design on another website, runnable through
// runner.js. The playground only generates text here; runner.js and embed.js
// define what the embedding page actually gets.

import { DEFAULTS, buildArgs, quoteForDisplay } from './options.js';
import { PALETTES, TYPES, tokenize } from './sv-highlight.js';

/**
 * Flags the embed needs beyond the ones the compile service always passes.
 * The service starts from the default options, so only the differences are
 * spelled out and a default design gets no attribute at all.
 */
export function embedArgs(options) {
  const base = buildArgs({ ...DEFAULTS, stage: 'run' });
  return buildArgs({ ...options, stage: 'run' })
    .filter((argument, index) => argument !== base[index])
    .map(quoteForDisplay)
    .join(' ');
}

const escapeText = (text) =>
  text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const escapeAttribute = (text) => escapeText(text).replace(/"/g, '&quot;');

function scriptTag(runnerUrl, args) {
  const data = args ? ` data-args="${escapeAttribute(args)}"` : '';
  return `<script src="${escapeAttribute(runnerUrl)}"${data} defer></script>`;
}

/** The source text a snippet contains; colors must be computed for this. */
export const snippetSource = (source) => source.replace(/^\n+|\s+$/g, '');

const TYPE_SET = new Set(TYPES);

// slang TokenKind and TriviaKind names, as `obelisk -dump-tokens` prints them.
const LISTING_KINDS = {
  LineComment: 'comment',
  BlockComment: 'comment',
  StringLiteral: 'string',
  IncludeFileName: 'string',
  IntegerLiteral: 'number',
  IntegerBase: 'number',
  UnbasedUnsizedLiteral: 'number',
  RealLiteral: 'number',
  TimeLiteral: 'number',
  SystemIdentifier: 'system',
  Directive: 'directive',
  MacroUsage: 'directive',
};

/**
 * Turn the driver's token listing for `source` into [kind, text] pairs like
 * tokenize() returns. slang decides what is a keyword for the chosen language
 * version; TYPES only picks which keywords get the type color. Returns null
 * when the listing does not describe this source.
 */
export function tokensFromListing(source, listing) {
  // Listing offsets count UTF-8 bytes; map them to string indices.
  const indexOfByte = new Map([[0, 0]]);
  let bytes = 0;
  for (let index = 0; index < source.length;) {
    const point = source.codePointAt(index);
    bytes += point < 0x80 ? 1 : point < 0x800 ? 2 : point < 0x10000 ? 3 : 4;
    index += point > 0xffff ? 2 : 1;
    indexOfByte.set(bytes, index);
  }

  const tokens = [];
  const push = (kind, text) => {
    if (!text) return;
    const last = tokens[tokens.length - 1];
    if (last && last[0] === kind) last[1] += text;
    else tokens.push([kind, text]);
  };
  let position = 0;
  // After a base ('h, 'sd, ...) slang lists the digits as separate tokens:
  // integers, identifiers (A5, ff), and ? for don't-care bits, possibly after
  // whitespace. They are all part of the number.
  let based = null;
  for (const line of listing.split('\n')) {
    const match = /^(\d+) (\d+) (\w+)$/.exec(line);
    if (!match) continue;
    const start = indexOfByte.get(Number(match[1]));
    const end = indexOfByte.get(Number(match[1]) + Number(match[2]));
    if (start === undefined || end === undefined || start < position) return null;
    const text = source.slice(start, end);
    let kind = LISTING_KINDS[match[3]] ?? '';
    if (match[3].endsWith('Keyword')) kind = TYPE_SET.has(text) ? 'type' : 'keyword';
    const gap = source.slice(position, start);
    if (based && /^[\da-fA-FxXzZ?_]+$/.test(text) &&
        (based === 'first' ? /^\s*$/.test(gap) : gap === '')) {
      kind = 'number';
      based = 'more';
    } else {
      based = match[3] === 'IntegerBase' ? 'first' : null;
    }
    push('', source.slice(position, start));
    push(kind, text);
    position = end;
  }
  push('', source.slice(position));
  return tokens.map(([, text]) => text).join('') === source ? tokens : null;
}

export const COLOR_CHOICES = ['light', 'dark', 'plain'];

/**
 * Colors are inline styles so the block looks the same on any site, with or
 * without scripts. "plain" leaves the code bare for a site's own highlighter
 * (or runner.js's fallback) to color.
 */
function colorize(source, colors, tokens) {
  const palette = PALETTES[colors];
  if (!palette) return { pre: '', codeStyle: '', code: escapeText(source) };
  const usable = tokens?.map(([, text]) => text).join('') === source;
  const code = (usable ? tokens : tokenize(source)).map(([kind, text]) => kind
    ? `<span style="color:${palette[kind]}">${escapeText(text)}</span>`
    : escapeText(text)).join('');
  const ground = `background:${palette.background};color:${palette.foreground}`;
  // The <code> repeats the ground because sites commonly style `code`
  // directly (Jekyll's minima: `pre, code { background-color: #eef }`), which
  // beats what it would inherit from the <pre>.
  return {
    pre: ` style="${ground};padding:12px 16px;border-radius:6px;overflow:auto"`,
    codeStyle: ` style="${ground}"`,
    code,
  };
}

/**
 * `tokens`, when given, are the [kind, text] pairs for snippetSource(source),
 * normally from tokensFromListing(); otherwise the built-in tokenizer colors
 * the snippet.
 */
export function htmlSnippet({
  source, options, runnerUrl, colors = 'plain', tokens = null,
}) {
  const args = embedArgs(options);
  const data = args ? ` data-obelisk-args="${escapeAttribute(args)}"` : '';
  const { pre, codeStyle, code } = colorize(snippetSource(source), colors, tokens);
  return [
    scriptTag(runnerUrl),
    '',
    `<pre${pre}><code class="language-systemverilog"${codeStyle}${data}>${code}</code></pre>`,
    '',
  ].join('\n');
}

/**
 * Markdown cannot attach attributes to a fenced block, so any flags go on the
 * script tag and apply to every block on the page.
 */
export function markdownSnippet({ source, options, runnerUrl }) {
  const body = snippetSource(source);
  const longest = Math.max(0, ...[...body.matchAll(/`+/g)].map((run) => run[0].length));
  const fence = '`'.repeat(Math.max(3, longest + 1));
  return [
    scriptTag(runnerUrl, embedArgs(options)),
    '',
    `${fence}systemverilog`,
    body,
    fence,
    '',
  ].join('\n');
}
