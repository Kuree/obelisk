// RUN: %python %source_root/test/Support/GenerateSDFCacheFileLimit.py %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err
// RUN: test ! -e %t.mlir

// IEEE 1800-2017 32.3 allows many annotation files, but static compilation
// admits a deterministic bounded cache rather than unbounded attacker-chosen
// file, byte, or parsed-entry state.

// CHECK-COUNT-1: error: static SDF annotation file-count resource limit exceeded
