// RUN: %python %source_root/test/Support/GenerateSDFResourceLimits.py scaled-boundary %t
// RUN: obelisk -emit-slang %t.sv -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2017 32.4/.5 exact decimal scaling cannot reject a large
// significand merely because its exponent would previously require a large
// power temporary. Only the 19 result digits and first discarded rounding
// digit are material to this exactly representable femtosecond delay.

// CHECK: timing_delay_count = 1 : i64
// CHECK-SAME: timing_delay_fs = array<i64: 9223372036854775799>
