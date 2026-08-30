// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py match-attempts %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// Production application (not parser coverage): every annotation matches only
// the final specify path. Bound the Clause 32.4 candidate cross product even
// though neither the parsed-entry nor successful-update limit is approached.
// CHECK-COUNT-1: error: static SDF annotation application-work resource limit exceeded
// CHECK-NOT: warning:
