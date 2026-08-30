// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py aggregate-bytes %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// Failed files remain charged to the deterministic Clause 32 static-input
// budget, so distinct malformed files cannot evade the aggregate byte limit.

// CHECK-COUNT-1: error: static SDF annotation aggregate byte resource limit exceeded
