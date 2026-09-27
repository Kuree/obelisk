// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// IEEE 1800-2017 Annex N threads a separate 32-bit seed through $dist_*.
// Both the variate and the updated seed must cross the native runtime ABI.

// CHECK-LABEL: llvm.func @draw(
// CHECK: llvm.call @obelisk_rt_v1_random_distribution(%{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}}, %[[RESULT_PTR:[0-9]+]], %[[SEED_PTR:[0-9]+]])
// CHECK: %[[RESULT:.*]] = llvm.load %[[RESULT_PTR]]
// CHECK: %[[NEXT_SEED:.*]] = llvm.load %[[SEED_PTR]]
// CHECK: %[[WITH_RESULT:.*]] = llvm.insertvalue %[[RESULT]], {{.*}}[0]
// CHECK: llvm.insertvalue %[[NEXT_SEED]], %[[WITH_RESULT]][1]

// The no-argument `$random` form uses the same Annex N algorithm through its
// dedicated context-global state, not the process-local `$urandom` ABI.
// CHECK-LABEL: llvm.func @legacy(
// CHECK: llvm.call @obelisk_rt_v1_random_legacy(%{{.*}}, %[[LEGACY_PTR:[0-9]+]])
// CHECK: %[[LEGACY:.*]] = llvm.load %[[LEGACY_PTR]]
// CHECK: llvm.insertvalue %[[LEGACY]], {{.*}}[0]

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @distribution {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "distribution.draw"
    simulation.code_unit.decl 2 in 0 function hierarchy "distribution.legacy"

    simulation.func private @draw(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %seed: i32 {simulation.capture_kind = 2 : i32}) -> (i32, i32)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %low = arith.constant 5 : i32
      %high = arith.constant 10 : i32
      %result, %next_seed = simulation.random.distribution
          %ctx, %seed, %low, %high {distribution = #simulation.random_distribution<uniform>} :
          (!simulation.context, i32, i32, i32) -> (i32, i32)
      simulation.return %result, %next_seed : i32, i32
    }

    simulation.func private @legacy(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32}) -> i32
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %result = simulation.random.legacy %ctx : (!simulation.context) -> i32
      simulation.return %result : i32
    }
  }
}
