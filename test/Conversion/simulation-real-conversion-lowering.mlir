// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-real-conversions)))' \
// RUN:   | FileCheck %s

// This test exercises standard-typed real conversion normalization as its own
// pass. Coroutine and bytecode lowering are intentionally absent.

module {
  obelisk_sim.design @time_lowering {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "time_process"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "real_function"
    obelisk_sim.scope.decl 0

    obelisk_sim.func @time_process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %delay: i64
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %ticks = obelisk_sim.time.constant 7
      %scaled = obelisk_sim.time.scale %delay by 4 signed = false : i64
      %sum = obelisk_sim.time.add %ticks, %scaled
      %to_real = obelisk_sim.time.to_real %delay by 100
      %from_real = obelisk_sim.time.from_real %to_real by 100 quantum 10
      obelisk_sim.suspend.delay %sum to ^second
    ^second:
      obelisk_sim.suspend.delay %from_real to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @real_function(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %integer: i32
            {obelisk_sim.capture_kind = 2 : i32},
        %wide: i1025
            {obelisk_sim.capture_kind = 2 : i32},
        %real: f64
            {obelisk_sim.capture_kind = 2 : i32})
        -> (f64, f32, f64, i32)
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %signed = obelisk_sim.real.from_integer %integer signed = true
          : i32 -> f64
      %short = obelisk_sim.real.from_integer %integer signed = true
          : i32 -> f32
      %overflow = obelisk_sim.real.from_integer %wide signed = false
          : i1025 -> f64
      %rounded = obelisk_sim.real.to_integer %real signed = true : i32
      obelisk_sim.return %signed, %short, %overflow, %rounded
          : f64, f32, f64, i32
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @time_process
// CHECK: obelisk_sim.time.constant 7
// CHECK: obelisk_sim.time.scale
// CHECK: obelisk_sim.time.add
// CHECK: arith.uitofp
// CHECK: arith.divf
// CHECK: obelisk_sim.time.from_real
// CHECK: obelisk_sim.suspend.delay
// CHECK-NOT: obelisk_sim.time.to_real
// CHECK-LABEL: obelisk_sim.func @real_function
// CHECK: arith.sitofp
// CHECK: arith.uitofp
// CHECK: arith.constant 0x7FF0000000000000 : f64
// CHECK: arith.bitcast
// CHECK: arith.shrui
// CHECK: obelisk_sim.return
// CHECK-NOT: obelisk_sim.real.
