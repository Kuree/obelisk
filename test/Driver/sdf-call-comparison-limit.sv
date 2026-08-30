// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py call-comparisons %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// Production application (not parser coverage): repeated same-file calls in
// one ordered initial block bypass the distinct-file cap but must not make the
// Clause 32.5/.6 call-order proof quadratic without a deterministic bound.
// CHECK-COUNT-1: error: static SDF annotation application-work resource limit exceeded
