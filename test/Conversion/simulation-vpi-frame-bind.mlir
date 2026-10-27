// RUN: talos-opt %s --encode-talos-sim-to-bytecode='vpi=full' --test-talos-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=full},talos-sim-verify-compute-graph,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LLVM
#word = #simulation.vpi_type<kind = int, isSigned = true, isFourState = false, range = [31, 0], children = [], childNames = []>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @binding {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.storage.decl 0 in 1 : i32 design hierarchy "top.result" {vpi_type = #word}
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.statement.decl 11 in 1 scope 1 type 33 name "block" {is_scope}
    simulation.statement_site.decl 1100 on 11 phase 0
    simulation.vpi_local.decl @local id 19 in 1 scope 1 within 11 ordinal 0 hierarchy "top.block.value" debug "value" : i32 {vpi_type = #word}
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = arith.constant 23 : i32
      %local = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      simulation.vpi.frame.bind %ctx, %local to @local : !simulation.ref<i32>
      %current = simulation.ref.load %local : !simulation.ref<i32> -> i32
      %output = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      simulation.ref.store %current to %output : i32, !simulation.ref<i32>
      simulation.return
    }
  }
}
// 0x11016 is the typed local-binding intrinsic; the declaration ID is an
// explicit operand, independent of object-table row numbers.
// BYTECODE: intrinsic {{[0-9]+}}: id=0x00011016 inputs=2 outputs=0 flags=0
// LLVM: llvm.func @talos_rt_v1_vpi_frame_bind(!llvm.ptr, i64, i64) -> i32
// LLVM: llvm.mlir.constant(19 : i64)
// LLVM: llvm.call @talos_rt_v1_vpi_frame_bind
// LLVM-NOT: simulation.vpi.frame.bind
