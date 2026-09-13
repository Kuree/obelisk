`define WIDTH 8
module m; // é
  logic [`WIDTH-1:0] v = 8'hA5;
  initial $display("x");
endmodule

// The source above must stay the first bytes of this file: the listing gives
// byte offsets, and the comment's "é" is two bytes.

// RUN: obelisk -dump-tokens %s | FileCheck %s
// RUN: obelisk -dump-tokens --std=1800-2017 %s -o %t.tokens
// RUN: FileCheck %s --input-file=%t.tokens

// The listing is the unpreprocessed text: directives and macro uses stay as
// written, comments are listed, and whitespace is not.
// CHECK:      0 7 Directive
// CHECK-NEXT: 8 5 Identifier
// CHECK-NEXT: 14 1 IntegerLiteral
// CHECK-NEXT: 16 6 ModuleKeyword
// CHECK-NEXT: 23 1 Identifier
// CHECK-NEXT: 24 1 Semicolon
// CHECK-NEXT: 26 5 LineComment
// CHECK-NEXT: 34 5 LogicKeyword
// CHECK-NEXT: 40 1 OpenBracket
// CHECK-NEXT: 41 6 Directive
// CHECK-NEXT: 47 1 Minus
// CHECK-NEXT: 48 1 IntegerLiteral
// CHECK-NEXT: 49 1 Colon
// CHECK-NEXT: 50 1 IntegerLiteral
// CHECK-NEXT: 51 1 CloseBracket
// CHECK-NEXT: 53 1 Identifier
// CHECK-NEXT: 55 1 Equals
// CHECK-NEXT: 57 1 IntegerLiteral
// CHECK-NEXT: 58 2 IntegerBase
// CHECK-NEXT: 60 2 Identifier
// CHECK-NEXT: 62 1 Semicolon
// CHECK-NEXT: 66 7 InitialKeyword
// CHECK-NEXT: 74 8 SystemIdentifier
// CHECK-NEXT: 82 1 OpenParenthesis
// CHECK-NEXT: 83 3 StringLiteral
// CHECK-NEXT: 86 1 CloseParenthesis
// CHECK-NEXT: 87 1 Semicolon
// CHECK-NEXT: 89 9 EndModuleKeyword
// CHECK-NEXT: {{[0-9]+}} {{[0-9]+}} LineComment
// CHECK-NOT:  Whitespace
// CHECK-NOT:  EndOfLine
// CHECK-NOT:  EndOfFile

// Several inputs are listed in order, each under its path.
// RUN: echo 'wire w;' > %t.second.sv
// RUN: obelisk -dump-tokens %s %t.second.sv | FileCheck %s --check-prefix=MULTI
// MULTI:      file {{.*}}dump-tokens.sv
// MULTI-NEXT: 0 7 Directive
// MULTI:      file {{.*}}.second.sv
// MULTI-NEXT: 0 4 WireKeyword
// MULTI-NEXT: 5 1 Identifier
// MULTI-NEXT: 6 1 Semicolon
// MULTI-NOT:  {{.}}

// Text that does not lex cleanly is still listed to the end, without
// diagnostics, because the listing describes text rather than a design.
// RUN: echo '"open' > %t.bad.sv
// RUN: echo "@@ 4'q wire" >> %t.bad.sv
// RUN: echo '/* never closed' >> %t.bad.sv
// RUN: echo 'wire' >> %t.bad.sv
// RUN: obelisk -dump-tokens %t.bad.sv 2> %t.bad.stderr | FileCheck %s --check-prefix=BAD
// RUN: test ! -s %t.bad.stderr
// BAD:      0 5 StringLiteral
// BAD-NEXT: 6 2 DoubleAt
// BAD-NEXT: 9 1 IntegerLiteral
// BAD-NEXT: 10 1 Apostrophe
// BAD-NEXT: 11 1 Identifier
// BAD-NEXT: 13 4 WireKeyword
// BAD-NEXT: 18 21 BlockComment
// BAD-NOT:  {{.}}

// RUN: not obelisk -dump-tokens %t.missing.sv 2>&1 | FileCheck %s --check-prefix=MISSING
// MISSING: obelisk: error: could not inspect input '{{.*}}missing.sv'
