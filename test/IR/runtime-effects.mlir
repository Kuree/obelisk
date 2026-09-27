// RUN: obelisk-opt --cse %s | FileCheck %s

// Runtime status failures update per-thread context error state. Stream reads
// also advance the cursor and may update EOF/error indicators, so neither kind
// of call is CSE-able.
// CHECK-LABEL: func.func @not_cseable
// CHECK-COUNT-2: runtime.file.getc
// CHECK-COUNT-2: runtime.file.eof
func.func @not_cseable(%ctx: !runtime.context, %fd: !runtime.fd)
    -> (!runtime.status, i8, !runtime.status, i8,
        !runtime.status, i32, !runtime.status, i32) {
  %s0, %b0 = runtime.file.getc %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i8)
  %s1, %b1 = runtime.file.getc %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i8)
  %s2, %e0 = runtime.file.eof %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i32)
  %s3, %e1 = runtime.file.eof %ctx, %fd :
      (!runtime.context, !runtime.fd) -> (!runtime.status, i32)
  return %s0, %b0, %s1, %b1, %s2, %e0, %s3, %e1 :
      !runtime.status, i8, !runtime.status, i8,
      !runtime.status, i32, !runtime.status, i32
}
