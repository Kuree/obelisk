// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py parsed-entries %t
// RUN: not obelisk -emit-slang %t.sv -o %t.mlir 2> %t.err
// RUN: FileCheck %s < %t.err

// Parsed files are charged by compact model entries as well as source bytes;
// IEEE 1800-2017 Clause 32 does not permit annotation text to create
// unbounded compiler-resident state.

// CHECK-COUNT-1: error: static SDF annotation parsed-entry resource limit exceeded
