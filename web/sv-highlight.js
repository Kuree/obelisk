// A SystemVerilog tokenizer small enough to run anywhere a snippet is shown.
//
// Baked embed snippets are colored from the compiler's own lexer
// (`obelisk -dump-tokens`); this approximation covers the cases where that is
// not available: runner.js coloring plain blocks on other sites, and the
// Embed dialog while the compiler is busy or unavailable. runner.js carries
// its own copy because it must stay one classic script that other sites load
// with a plain <script src>; a test keeps the two identical. The playground's
// editor uses the same word lists through sv-language.js.

export const KEYWORDS = [
  'module', 'endmodule', 'package', 'endpackage', 'program', 'endprogram',
  'interface', 'endinterface', 'class', 'endclass', 'function', 'endfunction',
  'task', 'endtask', 'begin', 'end', 'generate', 'endgenerate', 'case',
  'casex', 'casez', 'endcase', 'if', 'else', 'for', 'foreach', 'while',
  'do', 'repeat', 'forever', 'break', 'continue', 'return', 'fork', 'join',
  'join_any', 'join_none', 'initial', 'always', 'always_comb', 'always_ff',
  'always_latch', 'assign', 'wait', 'disable', 'default', 'extends',
  'implements', 'virtual', 'pure', 'local', 'protected', 'static',
  'automatic', 'const', 'ref', 'input', 'output', 'inout', 'parameter',
  'localparam', 'typedef', 'enum', 'struct', 'union', 'packed', 'tagged',
  'new', 'this', 'super', 'null', 'extern', 'import', 'export', 'randomize',
  'constraint', 'rand', 'randc', 'covergroup', 'endgroup', 'coverpoint',
  'cross', 'bins', 'property', 'endproperty', 'sequence', 'endsequence',
  'assert', 'assume', 'cover', 'expect', 'posedge', 'negedge', 'edge',
  'timeunit', 'timeprecision', 'unique', 'unique0', 'priority', 'solve',
  'before', 'inside', 'dist', 'with', 'matches', 'iff', 'genvar', 'defparam',
  'modport', 'clocking', 'endclocking', 'randcase', 'randsequence',
];

export const TYPES = [
  'logic', 'bit', 'reg', 'wire', 'byte', 'shortint', 'int', 'longint',
  'integer', 'time', 'real', 'shortreal', 'realtime', 'string', 'chandle',
  'event', 'void', 'signed', 'unsigned', 'tri', 'triand', 'trior', 'tri0',
  'tri1', 'trireg', 'wand', 'wor', 'supply0', 'supply1', 'uwire', 'var',
];

const KEYWORD_SET = new Set(KEYWORDS);
const TYPE_SET = new Set(TYPES);

// One alternative per token kind, tried in order at each position. Anything
// unmatched (operators, punctuation, whitespace) stays plain text.
const TOKEN = new RegExp([
  String.raw`(\/\*[\s\S]*?(?:\*\/|$)|\/\/[^\n]*)`,                  // 1 comment
  String.raw`("(?:[^"\\\n]|\\.)*"?)`,                                   // 2 string
  String.raw`(\$[A-Za-z_][\w$]*)`,                                         // 3 system task
  String.raw`(\x60[A-Za-z_]\w*)`,                                          // 4 directive
  String.raw`((?:\d[\d_]*\s*)?'[sS]?[bBoOdDhH]\s*[\da-fA-FxXzZ?_]+` +   // 5 number
    String.raw`|'[01xXzZ](?![\w'])` +
    String.raw`|\d[\d_]*(?:\.\d[\d_]*)?(?:[eE][-+]?\d+)?(?:[munpf]?s\b)?)`,
  String.raw`([A-Za-z_][\w$]*)`,                                            // 6 identifier
  String.raw`(\\\S+)`,                                                    // 7 escaped identifier
].join('|'), 'g');

/** Split source into [kind, text] pairs whose texts concatenate back to it. */
export function tokenize(source) {
  const tokens = [];
  const push = (kind, text) => {
    const last = tokens[tokens.length - 1];
    if (last && last[0] === kind) last[1] += text;
    else tokens.push([kind, text]);
  };
  let position = 0;
  for (const match of source.matchAll(TOKEN)) {
    if (match.index > position) push('', source.slice(position, match.index));
    const [text, comment, string, system, directive, number, word] = match;
    push(comment ? 'comment'
      : string ? 'string'
      : system ? 'system'
      : directive ? 'directive'
      : number ? 'number'
      : word ? (KEYWORD_SET.has(word) ? 'keyword' : TYPE_SET.has(word) ? 'type' : '')
      : '', text);
    position = match.index + text.length;
  }
  if (position < source.length) push('', source.slice(position));
  return tokens;
}

// Primer's syntax colors (GitHub's light and dark themes). Baked snippets also
// paint their own ground so they read the same on any site.
export const PALETTES = {
  light: {
    background: '#f6f8fa', foreground: '#1f2328',
    comment: '#6e7781', string: '#0a3069', keyword: '#cf222e', type: '#953800',
    number: '#0550ae', system: '#8250df', directive: '#116329',
  },
  dark: {
    background: '#0d1117', foreground: '#e6edf3',
    comment: '#8b949e', string: '#a5d6ff', keyword: '#ff7b72', type: '#ffa657',
    number: '#79c0ff', system: '#d2a8ff', directive: '#7ee787',
  },
};
