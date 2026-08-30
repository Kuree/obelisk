// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py wildcard-probes %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// Production application (not parser coverage): bounded empty wildcard CELLs
// still probe the elaborated instance inventory. Charge that Clause 32.4 work
// even though no IOPATH match or delay update occurs.
// CHECK-COUNT-1: error: static SDF annotation application-work resource limit exceeded
// CHECK-NOT: warning:
