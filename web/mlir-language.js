// MLIR language support for the read-only pipeline inspector. This is a
// tokenizer, not a parser: it follows MLIR's lexical forms closely enough to
// keep arbitrary dialects readable without needing a registry of operations.

export const MLIR_LANGUAGE_ID = 'mlir';

const KEYWORDS = [
  'affine_map', 'affine_set', 'attributes', 'builtin', 'dense', 'dense_resource',
  'else', 'false', 'ins', 'loc', 'module', 'null', 'opaque', 'outs', 'return',
  'sparse', 'true', 'unit', 'unknown', 'yield',
];

const TYPES = [
  'bf16', 'complex', 'f16', 'f32', 'f64', 'f80', 'f128', 'index', 'memref',
  'none', 'tensor', 'tuple', 'vector',
];

export function registerMlir(monaco) {
  monaco.languages.register({
    id: MLIR_LANGUAGE_ID,
    extensions: ['.mlir'],
    aliases: ['MLIR', 'mlir'],
  });

  monaco.languages.setMonarchTokensProvider(MLIR_LANGUAGE_ID, {
    defaultToken: '',
    keywords: KEYWORDS,
    types: TYPES,

    tokenizer: {
      root: [
        { include: '@whitespace' },

        // SSA values, block labels, symbol references, types, and attributes.
        [/%"(?:[^"\\]|\\.)*"/, 'variable'],
        [/%(?:[A-Za-z_$.-][\w$.-]*|\d+)/, 'variable'],
        [/\^(?:[A-Za-z_$.-][\w$.-]*|\d+)/, 'tag'],
        [/@"(?:[^"\\]|\\.)*"/, 'function'],
        [/@(?:[A-Za-z_$.-][\w$.-]*|\d+)/, 'function'],
        [/![A-Za-z_][\w$.-]*/, 'type'],
        [/#(?:[A-Za-z_][\w$.-]*|\d+)/, 'annotation'],
        [/[su]?i\d+/, 'type'],

        // Integer and floating-point literals, including hexadecimal forms.
        [/[+-]?0x[0-9a-fA-F]+/, 'number.hex'],
        [/[+-]?(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?/, 'number.float'],
        [/[+-]?\d+(?:[eE][+-]?\d+)?/, 'number'],

        // Bare dialect operations contain a dot. Builtin/custom assembly
        // keywords are classified separately below.
        [/"[A-Za-z_][\w$-]*(?:\.[A-Za-z_][\w$-]*)+"(?=\s*\()/, 'keyword'],
        [/[A-Za-z_][\w$-]*(?:\.[A-Za-z_][\w$-]*)+/, 'keyword'],
        [/[A-Za-z_][\w$-]*/, {
          cases: {
            '@types': 'type',
            '@keywords': 'keyword',
            '@default': '',
          },
        }],

        [/->|=>|=/, 'operator'],
        [/[{}()[\]<>:,]/, 'delimiter'],
        [/"/, 'string', '@string'],
      ],

      whitespace: [
        [/[ \t\r\n]+/, ''],
        [/\/\/.*$/, 'comment'],
      ],

      string: [
        [/[^\\"]+/, 'string'],
        [/\\./, 'string.escape'],
        [/"/, 'string', '@pop'],
      ],
    },
  });

  monaco.languages.setLanguageConfiguration(MLIR_LANGUAGE_ID, {
    comments: { lineComment: '//' },
    brackets: [['{', '}'], ['[', ']'], ['(', ')'], ['<', '>']],
    autoClosingPairs: [
      { open: '{', close: '}' },
      { open: '[', close: ']' },
      { open: '(', close: ')' },
      { open: '<', close: '>' },
      { open: '"', close: '"', notIn: ['string'] },
    ],
  });
}
