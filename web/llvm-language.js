// A compact LLVM IR language definition for Monaco. LLVM IR is regular enough
// that Monarch can provide useful highlighting without pulling in a TextMate
// grammar (and its runtime) for this read-only compiler output.

export const LLVM_LANGUAGE_ID = 'llvm';

export function registerLlvm(monaco) {
  monaco.languages.register({
    id: LLVM_LANGUAGE_ID,
    extensions: ['.ll'],
    aliases: ['LLVM IR', 'llvm'],
  });

  monaco.languages.setMonarchTokensProvider(LLVM_LANGUAGE_ID, {
    defaultToken: 'identifier',

    keywords: [
      'define', 'declare', 'global', 'constant', 'alias', 'ifunc',
      'private', 'internal', 'external', 'available_externally', 'linkonce',
      'linkonce_odr', 'weak', 'weak_odr', 'common', 'appending', 'extern_weak',
      'thread_local', 'local_unnamed_addr', 'unnamed_addr', 'dso_local',
      'dso_preemptable', 'hidden', 'protected', 'default', 'dllimport',
      'dllexport', 'cold', 'fastcc', 'ccc', 'tailcc', 'swiftcc', 'ghccc',
      'tail', 'musttail', 'notail', 'call', 'invoke', 'callbr', 'ret', 'br',
      'switch', 'indirectbr', 'resume', 'catchswitch', 'catchret', 'cleanupret',
      'unreachable', 'phi', 'select', 'freeze', 'va_arg', 'landingpad',
      'catchpad', 'cleanuppad', 'catch', 'filter', 'cleanup', 'alloca', 'load',
      'store', 'fence', 'cmpxchg', 'atomicrmw', 'getelementptr', 'extractelement',
      'insertelement', 'shufflevector', 'extractvalue', 'insertvalue', 'icmp',
      'fcmp', 'trunc', 'zext', 'sext', 'fptrunc', 'fpext', 'fptoui', 'fptosi',
      'uitofp', 'sitofp', 'ptrtoint', 'inttoptr', 'bitcast', 'addrspacecast',
      'add', 'fadd', 'sub', 'fsub', 'mul', 'fmul', 'udiv', 'sdiv', 'fdiv',
      'urem', 'srem', 'frem', 'shl', 'lshr', 'ashr', 'and', 'or', 'xor',
      'eq', 'ne', 'ugt', 'uge', 'ult', 'ule', 'sgt', 'sge', 'slt', 'sle',
      'oeq', 'ogt', 'oge', 'olt', 'ole', 'one', 'ord', 'ueq', 'uno',
      'true', 'false', 'null', 'none', 'undef', 'poison', 'zeroinitializer',
      'to', 'within', 'from', 'blockaddress', 'dso_local_equivalent',
      'no_cfi', 'distinct', 'uselistorder', 'uselistorder_bb', 'source_filename',
      'target', 'datalayout', 'triple', 'attributes', 'module', 'asm', 'sideeffect',
      'volatile', 'atomic', 'syncscope', 'acquire', 'release', 'acq_rel',
      'seq_cst', 'monotonic', 'unordered', 'nuw', 'nsw', 'exact', 'inbounds',
      'inrange', 'nneg', 'fast', 'nnan', 'ninf', 'nsz', 'arcp', 'contract',
      'afn', 'reassoc', 'align', 'addrspace', 'section', 'comdat', 'gc',
      'personality', 'prefix', 'prologue', 'partition',
      'noreturn', 'nounwind', 'readonly', 'readnone', 'writeonly', 'nocapture',
      'noalias', 'nonnull', 'dereferenceable', 'dereferenceable_or_null',
      'signext', 'zeroext', 'inreg', 'byval', 'byref', 'sret', 'nest', 'returned',
      'swiftself', 'swifterror', 'immarg', 'nocallback', 'nofree', 'nosync',
      'willreturn', 'mustprogress', 'speculatable', 'memory',
    ],

    typeKeywords: [
      'void', 'half', 'bfloat', 'float', 'double', 'fp128', 'x86_fp80',
      'ppc_fp128', 'label', 'metadata', 'token', 'x86_mmx', 'x86_amx', 'ptr',
    ],

    tokenizer: {
      root: [
        [/;.*$/, 'comment'],
        [/%"(?:[^"\\]|\\.)*"/, 'variable'],
        [/%(?:[-a-zA-Z$._][-a-zA-Z$._0-9]*|\d+)/, 'variable'],
        [/@"(?:[^"\\]|\\.)*"/, 'function'],
        [/@(?:[-a-zA-Z$._][-a-zA-Z$._0-9]*|\d+)/, 'function'],
        [/!(?:[-a-zA-Z$._][-a-zA-Z$._0-9]*|\d+)/, 'annotation'],
        [/#\d+/, 'annotation'],
        [/(?:[-a-zA-Z$._][-a-zA-Z$._0-9]*|\d+):/, 'tag'],
        [/i\d+\b/, 'type'],
        [/[a-zA-Z_][\w.]*/, {
          cases: {
            '@typeKeywords': 'type',
            '@keywords': 'keyword',
            '@default': 'identifier',
          },
        }],
        [/0x[0-9a-fA-F]+/, 'number.hex'],
        [/[+-]?(?:\d+\.\d*|\.\d+)(?:[eE][+-]?\d+)?/, 'number.float'],
        [/[+-]?\d+/, 'number'],
        [/c?"/, 'string', '@string'],
        [/[{}()[\]<>]/, '@brackets'],
        [/[,=*]/, 'delimiter'],
      ],
      string: [
        [/\\[0-9a-fA-F]{2}/, 'string.escape'],
        [/\\./, 'string.escape'],
        [/"/, 'string', '@pop'],
        [/[^\\"]+/, 'string'],
      ],
    },
  });

  monaco.languages.setLanguageConfiguration(LLVM_LANGUAGE_ID, {
    comments: { lineComment: ';' },
    brackets: [['{', '}'], ['[', ']'], ['(', ')'], ['<', '>']],
    autoClosingPairs: [
      { open: '{', close: '}' },
      { open: '[', close: ']' },
      { open: '(', close: ')' },
      { open: '<', close: '>' },
      { open: '"', close: '"' },
    ],
    surroundingPairs: [
      { open: '{', close: '}' },
      { open: '[', close: ']' },
      { open: '(', close: ')' },
      { open: '<', close: '>' },
      { open: '"', close: '"' },
    ],
  });
}
