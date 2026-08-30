// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py matched-updates %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// One compact wildcard CELL amplifies 1025 IOPATH records over 1025 matching
// instances. The compile-time Clause 32 update cap must stop the entire fold
// promptly and emit one diagnostic, without per-match errors or warnings.
// CHECK-COUNT-1: error: static SDF annotation-update resource limit exceeded
// CHECK-NOT: SDF delay is incompatible with target precision
// CHECK-NOT: warning:
