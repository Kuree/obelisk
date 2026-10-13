// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[RANDOM:.*]] = simulation.random.next
// CHECK: %[[INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[RESULT:.*]] = math.ceil %[[INPUT]] : f64
// CHECK: simulation.display {{.*}}(%[[RESULT]])
// CHECK: %[[FLOOR_INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[FLOOR_RESULT:.*]] = math.floor %[[FLOOR_INPUT]] : f64
// CHECK: simulation.display {{.*}}(%[[FLOOR_RESULT]])
// CHECK: %[[SQRT_INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[SQRT_RESULT:.*]] = math.sqrt %[[SQRT_INPUT]] : f64
// CHECK: simulation.display {{.*}}(%[[SQRT_RESULT]])
// CHECK: %[[EXP_INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[EXP_RESULT:.*]] = math.exp %[[EXP_INPUT]] : f64
// CHECK: simulation.display {{.*}}(%[[EXP_RESULT]])
// CHECK: %[[LN_INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[LN_RESULT:.*]] = math.log %[[LN_INPUT]] : f64
// CHECK: simulation.display {{.*}}(%[[LN_RESULT]])
// CHECK: %[[LOG10_INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[LOG10_RESULT:.*]] = math.log10 %[[LOG10_INPUT]] : f64
// CHECK: simulation.display {{.*}}(%[[LOG10_RESULT]])
// CHECK: %[[POW_BASE:.*]] = simulation.real.from_integer
// CHECK: %[[POW_EXPONENT:.*]] = simulation.real.from_integer
// CHECK: %[[POW_RESULT:.*]] = math.powf %[[POW_BASE]], %[[POW_EXPONENT]] : f64
// CHECK: simulation.display {{.*}}(%[[POW_RESULT]])
// CHECK: %[[ATAN2_Y:.*]] = simulation.real.from_integer
// CHECK: %[[ATAN2_X:.*]] = simulation.real.from_integer
// CHECK: %[[ATAN2_RESULT:.*]] = math.atan2 %[[ATAN2_Y]], %[[ATAN2_X]] : f64
// CHECK: simulation.display {{.*}}(%[[ATAN2_RESULT]])
// CHECK: %[[HYPOT_X:.*]] = simulation.real.from_integer
// CHECK: %[[HYPOT_Y:.*]] = simulation.real.from_integer
// CHECK: %[[HYPOT_X_SQUARED:.*]] = arith.mulf %[[HYPOT_X]], %[[HYPOT_X]] : f64
// CHECK: %[[HYPOT_Y_SQUARED:.*]] = arith.mulf %[[HYPOT_Y]], %[[HYPOT_Y]] : f64
// CHECK: %[[HYPOT_SUM:.*]] = arith.addf %[[HYPOT_X_SQUARED]], %[[HYPOT_Y_SQUARED]] : f64
// CHECK: %[[HYPOT_RESULT:.*]] = math.sqrt %[[HYPOT_SUM]] : f64
// CHECK: simulation.display {{.*}}(%[[HYPOT_RESULT]])
// IEEE 1800-2017 20.15.1 defines $random as a signed 32-bit integer.
// CHECK: %[[ITOR_INPUT:.*]] = simulation.random.legacy
// CHECK: %[[ITOR_RESULT:.*]] = simulation.real.from_integer %[[ITOR_INPUT]] signed = true : i32 -> f64
// CHECK: simulation.display {{.*}}(%[[ITOR_RESULT]])
// CHECK: %[[RTOI_REAL:.*]] = simulation.real.from_integer
// Truncation uses integer IEEE operations admitted by design bytecode.
// CHECK: arith.bitcast %[[RTOI_REAL]] : f64 to i64
// CHECK-NOT: math.trunc
// CHECK: %[[RTOI_TRUNCATED:.*]] = arith.bitcast {{.*}} : i64 to f64
// CHECK: %[[RTOI_RESULT:.*]] = simulation.real.to_integer %[[RTOI_TRUNCATED]]
// $rtoi returns a four-state integer; a non-finite operand produces X.
// CHECK: %[[RTOI_BITS:.*]] = simulation.logic.from_bits %[[RTOI_RESULT]]
// CHECK: %[[RTOI_FINITE:.*]] = arith.cmpi ne
// CHECK: %[[RTOI_VALUE:.*]] = arith.select %[[RTOI_FINITE]], %[[RTOI_BITS]]
// CHECK: simulation.display {{.*}}(%[[RTOI_VALUE]])
// CHECK: %[[BITSTOREAL_BITS:.*]] = arith.extui
// CHECK: %[[BITSTOREAL_RESULT:.*]] = arith.bitcast %[[BITSTOREAL_BITS]] : i64 to f64
// CHECK: simulation.display {{.*}}(%[[BITSTOREAL_RESULT]])
// CHECK: %[[REALTOBITS_INPUT:.*]] = simulation.real.from_integer
// CHECK: %[[REALTOBITS_RESULT:.*]] = arith.bitcast %[[REALTOBITS_INPUT]] : f64 to i64
// CHECK: simulation.display {{.*}}(%[[REALTOBITS_RESULT]])
// CHECK: %[[BITSTOSHORTREAL_BITS:.*]] = arith.trunci
// CHECK: %[[BITSTOSHORTREAL_RESULT:.*]] = arith.bitcast %[[BITSTOSHORTREAL_BITS]] : i32 to f32
// CHECK: %[[BITSTOSHORTREAL_WIDE:.*]] = arith.extf %[[BITSTOSHORTREAL_RESULT]] : f32 to f64
// CHECK: simulation.display {{.*}}(%[[BITSTOSHORTREAL_WIDE]])
// CHECK: %[[SHORTREALTOBITS_REAL:.*]] = simulation.real.from_integer
// CHECK: %[[SHORTREALTOBITS_INPUT:.*]] = arith.truncf %[[SHORTREALTOBITS_REAL]] : f64 to f32
// CHECK: %[[SHORTREALTOBITS_RESULT:.*]] = arith.bitcast %[[SHORTREALTOBITS_INPUT]] : f32 to i32
// CHECK: simulation.display {{.*}}(%[[SHORTREALTOBITS_RESULT]])

`timescale 1ns / 1ns
module top;
  initial begin
    $display($ceil($urandom));
    $display($floor($urandom));
    $display($sqrt($urandom));
    $display($exp($urandom));
    $display($ln($urandom));
    $display($log10($urandom));
    $display($pow($urandom, $urandom));
    $display($atan2($urandom, $urandom));
    $display($hypot($urandom, $urandom));
    $display($itor($random));
    $display($rtoi($itor($random)));
    $display($bitstoreal($urandom));
    $display($realtobits($itor($random)));
    $display($bitstoshortreal($urandom));
    $display($shortrealtobits($itor($urandom)));
  end
endmodule
