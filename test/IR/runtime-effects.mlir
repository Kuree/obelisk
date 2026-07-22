// RUN: obelisk-opt --cse %s | FileCheck %s

// Runtime status failures update per-thread context error state. Stream reads
// also advance the cursor and may update EOF/error indicators, so neither kind
// of call is CSE-able.
// CHECK-LABEL: func.func @not_cseable
// CHECK-COUNT-2: obelisk_rt.file.getc
// CHECK-COUNT-2: obelisk_rt.file.eof
func.func @not_cseable(%ctx: !obelisk_rt.context, %fd: !obelisk_rt.fd)
    -> (!obelisk_rt.status, i8, !obelisk_rt.status, i8,
        !obelisk_rt.status, i32, !obelisk_rt.status, i32) {
  %s0, %b0 = obelisk_rt.file.getc %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i8)
  %s1, %b1 = obelisk_rt.file.getc %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i8)
  %s2, %e0 = obelisk_rt.file.eof %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i32)
  %s3, %e1 = obelisk_rt.file.eof %ctx, %fd :
      (!obelisk_rt.context, !obelisk_rt.fd) -> (!obelisk_rt.status, i32)
  return %s0, %b0, %s1, %b1, %s2, %e0, %s3, %e1 :
      !obelisk_rt.status, i8, !obelisk_rt.status, i8,
      !obelisk_rt.status, i32, !obelisk_rt.status, i32
}
