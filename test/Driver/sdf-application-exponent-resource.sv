// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py exponent %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// Production application/folding (not parser coverage): IEEE 1800-2017
// 32.4/.5 require the typed exact exponent spelling to be converted at the
// destination precision. Repeated enormous positive and negative orders must
// be classified linearly; neither may allocate an exponent-sized integer or
// power-of-ten temporary.

// CHECK-COUNT-64: error: SDF delay is incompatible with target precision
// CHECK-NOT: IOPATH requires 1, 2, 3, 6, or 12 delay values
