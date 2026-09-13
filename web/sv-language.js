// Monaco does not ship a SystemVerilog mode, so register a Monarch tokenizer.
// Scope: enough of IEEE 1800 to read well in an editor, not a parser.

import { KEYWORDS, TYPES } from './sv-highlight.js';

export const SV_LANGUAGE_ID = 'systemverilog';

export function registerSystemVerilog(monaco) {
  monaco.languages.register({
    id: SV_LANGUAGE_ID,
    extensions: ['.sv', '.svh', '.v', '.vh'],
    aliases: ['SystemVerilog', 'systemverilog'],
  });

  monaco.languages.setMonarchTokensProvider(SV_LANGUAGE_ID, {
    defaultToken: '',
    keywords: KEYWORDS,
    typeKeywords: TYPES,
    operators: [
      '=', '<=', '==', '===', '!=', '!==', '=?=', '+', '-', '*', '/', '%',
      '**', '&&', '||', '!', '&', '|', '^', '~', '<<', '>>', '<<<', '>>>',
      '<', '>', '?', ':', '->', '|->', '|=>', '##', '::', '++', '--',
    ],
    symbols: /[=><!~?:&|+\-*/^%#]+/,

    tokenizer: {
      root: [
        // System tasks and functions: $display, $finish, $urandom, ...
        [/\$[a-zA-Z_]\w*/, 'predefined'],
        // Compiler directives: `include, `define, `timescale, ...
        [/`[a-zA-Z_]\w*/, 'keyword.directive'],

        [/[a-zA-Z_]\w*/, {
          cases: {
            '@typeKeywords': 'type',
            '@keywords': 'keyword',
            '@default': 'identifier',
          },
        }],

        { include: '@whitespace' },

        // Sized literals: 8'hAF, 4'b10x1, 32'd100, 'sd7
        [/\d*'[sS]?[bB][\s]*[01xXzZ?_]+/, 'number.binary'],
        [/\d*'[sS]?[oO][\s]*[0-7xXzZ?_]+/, 'number.octal'],
        [/\d*'[sS]?[dD][\s]*[0-9xXzZ?_]+/, 'number'],
        [/\d*'[sS]?[hH][\s]*[0-9a-fA-FxXzZ?_]+/, 'number.hex'],
        [/\d+\.\d+([eE][-+]?\d+)?/, 'number.float'],
        [/[\d_]+/, 'number'],

        [/[{}()[\]]/, '@brackets'],
        [/@symbols/, { cases: { '@operators': 'operator', '@default': '' } }],

        [/"/, 'string', '@string'],
        [/[;,.]/, 'delimiter'],
      ],

      whitespace: [
        [/[ \t\r\n]+/, ''],
        [/\/\*/, 'comment', '@comment'],
        [/\/\/.*$/, 'comment'],
      ],

      comment: [
        [/[^/*]+/, 'comment'],
        [/\*\//, 'comment', '@pop'],
        [/[/*]/, 'comment'],
      ],

      string: [
        [/[^\\"]+/, 'string'],
        [/\\./, 'string.escape'],
        [/"/, 'string', '@pop'],
      ],
    },
  });

  monaco.languages.setLanguageConfiguration(SV_LANGUAGE_ID, {
    comments: { lineComment: '//', blockComment: ['/*', '*/'] },
    brackets: [['{', '}'], ['[', ']'], ['(', ')']],
    autoClosingPairs: [
      { open: '{', close: '}' },
      { open: '[', close: ']' },
      { open: '(', close: ')' },
      { open: '"', close: '"', notIn: ['string'] },
    ],
    // Indent inside the block-ish constructs people actually nest.
    indentationRules: {
      increaseIndentPattern:
        /^\s*(begin|fork|case[xz]?|module|class|function|task|interface|package|program|generate|covergroup|property|sequence|clocking)\b.*$/,
      decreaseIndentPattern:
        /^\s*(end|endcase|endmodule|endclass|endfunction|endtask|endinterface|endpackage|endprogram|endgenerate|endgroup|endproperty|endsequence|endclocking|join|join_any|join_none)\b.*$/,
    },
  });
}
