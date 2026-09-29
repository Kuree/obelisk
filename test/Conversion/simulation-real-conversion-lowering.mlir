// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-real-conversions)))' \
// RUN:   | FileCheck %s

// This test exercises standard-typed real conversion normalization as its own
// pass. Coroutine and bytecode lowering are intentionally absent.

module {
  simulation.design @time_lowering {
    simulation.code_unit.decl 1 in 0 initial hierarchy "time_process"
    simulation.code_unit.decl 2 in 0 function hierarchy "real_function"
    simulation.scope.decl 0

    simulation.func @time_process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %delay: i64
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %ticks = simulation.time.constant 7
      %scaled = simulation.time.scale %delay by 4 signed = false : i64
      %sum = simulation.time.add %ticks, %scaled
      %to_real = simulation.time.to_real %delay by 100
      %from_real = simulation.time.from_real %to_real by 100 quantum 10
      simulation.suspend.delay %sum to ^second
    ^second:
      simulation.suspend.delay %from_real to ^done
    ^done:
      simulation.return
    }

    simulation.func @real_function(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %integer: i32
            {simulation.capture_kind = 2 : i32},
        %wide: i1025
            {simulation.capture_kind = 2 : i32},
        %real: f64
            {simulation.capture_kind = 2 : i32})
        -> (f64, f32, f64, i32)
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %signed = simulation.real.from_integer %integer signed = true
          : i32 -> f64
      %short = simulation.real.from_integer %integer signed = true
          : i32 -> f32
      %overflow = simulation.real.from_integer %wide signed = false
          : i1025 -> f64
      %rounded = simulation.real.to_integer %real signed = true : i32
      simulation.return %signed, %short, %overflow, %rounded
          : f64, f32, f64, i32
    }
  }
}

// CHECK-LABEL: simulation.func @time_process
// CHECK: simulation.time.constant 7
// CHECK: simulation.time.scale
// CHECK: simulation.time.add
// CHECK: arith.uitofp
// CHECK: arith.divf
// CHECK: simulation.time.from_real
// CHECK: simulation.suspend.delay
// CHECK-NOT: simulation.time.to_real
// CHECK-LABEL: simulation.func @real_function
// CHECK: arith.sitofp
// With no AArch64 target triple, retain LLVM's direct arbitrary-width
// conversion path rather than expanding it for targets such as x86.
// CHECK: arith.uitofp {{.*}} : i1025 to f64
// CHECK-NOT: math.ctlz
// CHECK: arith.bitcast
// CHECK: arith.shrui
// CHECK: simulation.return
// CHECK-NOT: simulation.real.
