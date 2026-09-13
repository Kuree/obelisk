// REQUIRES: node
// RUN: %node --experimental-default-type=module %s

import assert from 'node:assert/strict';

import { DEFAULTS, splitFlags } from '../../web/options.js';
import {
  COLOR_CHOICES, embedArgs, htmlSnippet, markdownSnippet, snippetSource,
  tokensFromListing,
} from '../../web/embed-snippet.js';
import { PALETTES } from '../../web/sv-highlight.js';

const runnerUrl = 'https://example.com/obelisk/runner.js';

// The compile service already passes the defaults, whatever stage is open.
assert.equal(embedArgs(DEFAULTS), '');
assert.equal(embedArgs({ ...DEFAULTS, stage: 'slang' }), '');
const custom = {
  ...DEFAULTS,
  opt: '-O0',
  tier: 'bytecode',
  top: 'tb',
  defines: 'NAME="a b"',
};
assert.equal(embedArgs(custom), `-O0 --execution-tier=bytecode --top=tb '-DNAME=a b'`);
assert.deepEqual(splitFlags(embedArgs(custom)),
  ['-O0', '--execution-tier=bytecode', '--top=tb', '-DNAME=a b']);

const source = '\nmodule top;\n  initial if (1 < 2 && 3 > 2) $display("ok");\nendmodule\n\n';

assert.equal(htmlSnippet({ source, options: DEFAULTS, runnerUrl }), [
  '<script src="https://example.com/obelisk/runner.js" defer></script>',
  '',
  '<pre><code class="language-systemverilog">module top;',
  '  initial if (1 &lt; 2 &amp;&amp; 3 &gt; 2) $display("ok");',
  'endmodule</code></pre>',
  '',
].join('\n'));
assert.match(htmlSnippet({ source, options: custom, runnerUrl }),
  /<pre><code class="language-systemverilog" data-obelisk-args="-O0 --execution-tier=bytecode --top=tb '-DNAME=a b'">/);
assert.match(htmlSnippet({ source, options: { ...DEFAULTS, defines: 'S="x"' }, runnerUrl }),
  /data-obelisk-args="-DS=x"/);
assert.match(htmlSnippet({ source, options: { ...DEFAULTS, extra: `'--x=a "b"'` }, runnerUrl }),
  /data-obelisk-args="'--x=a &quot;b&quot;'"/);

assert.equal(markdownSnippet({ source, options: DEFAULTS, runnerUrl }), [
  '<script src="https://example.com/obelisk/runner.js" defer></script>',
  '',
  '```systemverilog',
  'module top;',
  '  initial if (1 < 2 && 3 > 2) $display("ok");',
  'endmodule',
  '```',
  '',
].join('\n'));
assert.match(markdownSnippet({ source, options: custom, runnerUrl }),
  /^<script src="[^"]+" data-args="-O0 --execution-tier=bytecode --top=tb '-DNAME=a b'" defer><\/script>\n/);

// A fence must be longer than any backtick run inside the source.
const fenced = markdownSnippet({ source: '`define A 1\n// ````\n', options: DEFAULTS, runnerUrl });
assert.match(fenced, /\n`````systemverilog\n`define A 1\n\/\/ ````\n`````\n$/);

// Baked colors are inline styles on spans inside the runnable <code>, so the
// runner still finds the block and reads the same source text back.
assert.deepEqual(COLOR_CHOICES, ['light', 'dark', 'plain']);
const baked = htmlSnippet({ source, options: custom, runnerUrl, colors: 'dark' });
assert.match(baked, new RegExp(
  `<pre style="background:${PALETTES.dark.background};color:${PALETTES.dark.foreground};[^"]*">` +
  // The <code> repeats the ground so a site's own `code` styles cannot
  // override the colors it would inherit.
  `<code class="language-systemverilog" style="background:${PALETTES.dark.background};` +
  `color:${PALETTES.dark.foreground}" data-obelisk-args="[^"]+">` +
  `<span style="color:${PALETTES.dark.keyword}">module</span> top;`));
assert.match(baked, new RegExp(`<span style="color:${PALETTES.dark.system}">\\$display</span>`));
assert.match(baked, new RegExp(`<span style="color:${PALETTES.dark.string}">&quot;ok&quot;</span>|<span style="color:${PALETTES.dark.string}">"ok"</span>`));
const bakedText = baked.match(/<code[^>]*>([\s\S]*)<\/code>/)[1]
  .replace(/<[^>]+>/g, '').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
assert.equal(bakedText, source.trim());
assert.match(htmlSnippet({ source, options: DEFAULTS, runnerUrl, colors: 'light' }),
  new RegExp(`<pre style="background:${PALETTES.light.background};`));
assert.equal(htmlSnippet({ source, options: DEFAULTS, runnerUrl, colors: 'plain' }),
  htmlSnippet({ source, options: DEFAULTS, runnerUrl }));

// `obelisk -dump-tokens` listings: byte offsets, slang kind names, and no
// whitespace. This one is the driver's real output for `listed`; "é" is two
// UTF-8 bytes, so later offsets run one ahead of the string indices.
const listed = "// é\nlogic [7:0] v = 8'hA5; initial $display(\"x\", 4'b10?z, " +
  "16 'h ff, 'sd7, 'x, 1.5e3, 10ns);\n`define W 8";
const listing = [
  '0 5 LineComment', '6 5 LogicKeyword', '12 1 OpenBracket', '13 1 IntegerLiteral',
  '14 1 Colon', '15 1 IntegerLiteral', '16 1 CloseBracket', '18 1 Identifier',
  '20 1 Equals', '22 1 IntegerLiteral', '23 2 IntegerBase', '25 2 Identifier',
  '27 1 Semicolon', '29 7 InitialKeyword', '37 8 SystemIdentifier',
  '45 1 OpenParenthesis', '46 3 StringLiteral', '49 1 Comma', '51 1 IntegerLiteral',
  '52 2 IntegerBase', '54 2 IntegerLiteral', '56 1 Question', '57 1 Identifier',
  '58 1 Comma', '60 2 IntegerLiteral', '63 2 IntegerBase', '66 2 Identifier',
  '68 1 Comma', '70 3 IntegerBase', '73 1 IntegerLiteral', '74 1 Comma',
  '76 2 UnbasedUnsizedLiteral', '78 1 Comma', '80 5 RealLiteral', '85 1 Comma',
  '87 4 TimeLiteral', '91 1 CloseParenthesis', '92 1 Semicolon', '94 7 Directive',
  '102 1 Identifier', '104 1 IntegerLiteral', '',
].join('\n');
assert.deepEqual(tokensFromListing(listed, listing), [
  ['comment', '// é'], ['', '\n'], ['type', 'logic'], ['', ' ['], ['number', '7'],
  ['', ':'], ['number', '0'], ['', '] v = '], ['number', "8'hA5"], ['', '; '],
  ['keyword', 'initial'], ['', ' '], ['system', '$display'], ['', '('],
  ['string', '"x"'], ['', ', '], ['number', "4'b10?z"], ['', ', '],
  ['number', '16'], ['', ' '], ['number', "'h"], ['', ' '], ['number', 'ff'], ['', ', '], ['number', "'sd7"],
  ['', ', '], ['number', "'x"], ['', ', '], ['number', '1.5e3'], ['', ', '],
  ['number', '10ns'], ['', ');\n'], ['directive', '`define'], ['', ' W '],
  ['number', '8'],
]);
// Digits must follow their base: an identifier after other tokens stays plain.
assert.deepEqual(
  tokensFromListing("'h ; ff", '0 2 IntegerBase\n3 1 Semicolon\n5 2 Identifier'),
  [['number', "'h"], ['', ' ; ff']]);
// A listing for different text, or one that overlaps itself, is rejected so
// the snippet falls back to the built-in tokenizer.
assert.equal(tokensFromListing('logic', '0 9 LogicKeyword'), null);
assert.equal(tokensFromListing('logic v', '0 5 LogicKeyword\n2 1 Identifier'), null);
assert.deepEqual(tokensFromListing('', ''), []);

// Listed tokens drive the baked colors; ones that do not match are ignored.
const fromListing = htmlSnippet({
  source: 'module m; endmodule\n', options: DEFAULTS, runnerUrl, colors: 'light',
  tokens: [['keyword', 'module'], ['', ' m; '], ['type', 'endmodule']],
});
assert.match(fromListing, new RegExp(`<span style="color:${PALETTES.light.type}">endmodule</span>`));
assert.equal(
  htmlSnippet({
    source: 'module m; endmodule\n', options: DEFAULTS, runnerUrl, colors: 'light',
    tokens: [['type', 'something else']],
  }),
  htmlSnippet({ source: 'module m; endmodule\n', options: DEFAULTS, runnerUrl, colors: 'light' }));
assert.equal(snippetSource('\n\nmodule m;\nendmodule\n\n'), 'module m;\nendmodule');

console.log('web embed snippets OK');
