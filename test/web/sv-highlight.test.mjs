// REQUIRES: node
// RUN: %node --experimental-default-type=module %s

import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';

import { EXAMPLES } from '../../web/examples.js';
import { KEYWORDS, PALETTES, TYPES, tokenize } from '../../web/sv-highlight.js';

const kinds = (source) => tokenize(source).filter(([kind]) => kind);

assert.deepEqual(kinds(`\`timescale 1ns/1ps
module m #(parameter int W = 8) (input logic [W-1:0] a);
  /* block
     comment */ wire \\esc$aped ;
  initial begin // line comment
    $display("a=%h \\"q\\"", 8'hA_5, 'x, 4'b10?z, 1.5e3, 10ns);
  end
endmodule`), [
  ['directive', '`timescale'], ['number', '1ns'], ['number', '1ps'],
  ['keyword', 'module'], ['keyword', 'parameter'], ['type', 'int'], ['number', '8'],
  ['keyword', 'input'], ['type', 'logic'], ['number', '1'], ['number', '0'],
  ['comment', '/* block\n     comment */'], ['type', 'wire'],
  ['keyword', 'initial'], ['keyword', 'begin'], ['comment', '// line comment'],
  ['system', '$display'], ['string', '"a=%h \\"q\\""'], ['number', "8'hA_5"],
  ['number', "'x"], ['number', "4'b10?z"], ['number', '1.5e3'], ['number', '10ns'],
  ['keyword', 'end'], ['keyword', 'endmodule'],
]);

// Identifiers are whole words: no keyword or number inside them.
assert.deepEqual(kinds('endmodule_x reg1 a1b2 begin_'), []);
// Unterminated strings and comments still cover the rest of the text.
assert.deepEqual(kinds('"open'), [['string', '"open']]);
assert.deepEqual(kinds('/* open\nstill'), [['comment', '/* open\nstill']]);

for (const word of KEYWORDS) assert.deepEqual(kinds(word), [['keyword', word]]);
for (const word of TYPES) assert.deepEqual(kinds(word), [['type', word]]);

// Tokens always concatenate back to the source, which the runner relies on.
const corpus = [
  ...EXAMPLES.map((example) => example.source),
  "a <= b ? 'z : {<<{c}}; #1step; @(posedge clk iff en) \\weird+name x;",
];
for (const source of corpus) {
  assert.equal(tokenize(source).map(([, text]) => text).join(''), source);
}

// runner.js is a classic script with its own copy of the tokenizer and
// palettes; load it with just enough of a page to reach its exports.
const web = new URL('../../web/', import.meta.url);
const runnerSource = await readFile(new URL('runner.js', web), 'utf8');
const page = {
  location: { href: 'https://example.com/post.html' },
  document: { readyState: 'complete', currentScript: null, querySelectorAll: () => [] },
  URL,
  setTimeout,
  clearTimeout,
};
page.window = page;
vm.runInNewContext(runnerSource, page);
for (const source of [...corpus, ...KEYWORDS, ...TYPES]) {
  assert.deepEqual(
    JSON.parse(JSON.stringify(page.ObeliskRunner.tokenize(source))),
    tokenize(source));
}
for (const [theme, scope] of [['light', 'obelisk-hl'], ['dark', 'obelisk-hl-dark']]) {
  for (const [kind, color] of Object.entries(PALETTES[theme])) {
    if (kind === 'background' || kind === 'foreground') continue;
    assert.ok(
      runnerSource.includes(`:where(.${scope}) :where(.obelisk-${kind}) { color: ${color}; }`),
      `runner.js ${theme} ${kind} color should be ${color}`);
  }
}

// Site generators that put the language class on a wrapper are found too.
for (const selector of [
  'pre > code.language-systemverilog', 'pre > code.language-sv',
  '.language-systemverilog pre > code', '.language-sv pre > code',
]) {
  assert.ok(runnerSource.includes(`'${selector}'`), `runner.js default selector: ${selector}`);
}

// The coverage report page colors source like the site does, from its own
// copy of tokensFromListing() and TYPES; keep the copies identical.
{
  const report = await readFile(
    new URL('../../tools/obelisk-cov/CoverageReport.js', import.meta.url), 'utf8');
  const embed = await readFile(new URL('embed-snippet.js', web), 'utf8');
  const copied = report.split('// BEGIN tokensFromListing (copy of web/embed-snippet.js)\n')[1]
    ?.split('// END tokensFromListing')[0];
  const original = embed.slice(embed.indexOf('// slang TokenKind and TriviaKind names'),
                               embed.indexOf('export const COLOR_CHOICES'))
    .replace('export function tokensFromListing', 'function tokensFromListing');
  assert.ok(copied, 'CoverageReport.js marks its copy of tokensFromListing');
  assert.equal(copied.trim(), original.trim());
  const types = report.match(/^const TYPES = (\[[\s\S]*?\]);$/m);
  assert.ok(types, 'CoverageReport.js has a TYPES list');
  assert.deepEqual(JSON.parse(JSON.stringify(vm.runInNewContext(types[1]))), TYPES);
}

console.log('web SystemVerilog highlighting OK');
