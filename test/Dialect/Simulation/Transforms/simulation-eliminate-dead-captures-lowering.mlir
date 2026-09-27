// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off})' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-captures,simulation.func(canonicalize,cse),symbol-dce,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off})' | FileCheck %s --check-prefix=O1
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries,simulation.func(canonicalize,cse),symbol-dce,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off})' | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries,simulation.func(canonicalize,cse)),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // O0: obelisk.bytecode.image
  // O1: obelisk.bytecode.image
  // BOUNDARY: obelisk.bytecode.image
  // NATIVE: llvm.func @writer(%{{.*}}: !llvm.ptr, %{{.*}}: i64, %{{.*}}: i32) attributes
  // NATIVE: llvm.call @writer({{.*}}) : (!llvm.ptr, i64, i32) -> ()
  simulation.design @lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.writer"
    simulation.storage.decl 0 in 0 : i32 design

    // O0-LABEL: simulation.func private @process(
    // O0-SAME: !simulation.context
    // O0-SAME: }, %arg1: i32
    // O1-LABEL: simulation.func private @process(
    // O1-SAME: !simulation.context {simulation.capture_kind = 0 : i32}) attributes
    simulation.func private @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }

    // BOUNDARY-LABEL: simulation.func private @writer(
    // BOUNDARY-NOT: -> i32
    // BOUNDARY: simulation.return
    simulation.func private @writer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.ref.store %value to %storage : i32, !simulation.ref<i32>
      simulation.return %value : i32
    }

    // O0-LABEL: simulation.func @root(
    // O0: simulation.spawn @process(%arg0, %c0_i32)
    // O1-LABEL: simulation.func @root(
    // O1: simulation.spawn @process(%arg0)
    // BOUNDARY-LABEL: simulation.func @root(
    // BOUNDARY: simulation.spawn @process(%arg0)
    // BOUNDARY: simulation.call @writer(%arg0, %{{.*}}, %{{.*}}) : (!simulation.context, !simulation.ref<i32>, i32) -> ()
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      %child = simulation.spawn @process(%ctx, %zero)
          : !simulation.context, i32 -> !simulation.process
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %result = simulation.call @writer(%ctx, %storage, %zero)
          : (!simulation.context, !simulation.ref<i32>, i32) -> i32
      simulation.return
    }
  }
}
