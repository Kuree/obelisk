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

// Tools such as obelisk-cov lex through the ObeliskSourceTokens library
// directly; it must list exactly what the driver prints.
// RUN: obelisk -dump-tokens --std=1800-2023 %s -o %t.driver.tokens
// RUN: %source-tokens-test --std=1800-2023 %s | diff %t.driver.tokens -
// RUN: %source-tokens-test --std=1800-2017 %s | diff %t.tokens -

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
// RUN: obelisk -dump-tokens %s %S/Inputs/dump-tokens.second.sv | FileCheck %s --check-prefix=MULTI
// MULTI:      file {{.*}}dump-tokens.sv
// MULTI-NEXT: 0 7 Directive
// MULTI:      file {{.*}}.second.sv
// MULTI-NEXT: 0 4 WireKeyword
// MULTI-NEXT: 5 1 Identifier
// MULTI-NEXT: 6 1 Semicolon
// MULTI-NOT:  {{.}}
