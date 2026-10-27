// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-inline{opt-level=3}))' | FileCheck %s --check-prefix=INLINE
// RUN: talos-opt %s --encode-talos-sim-to-bytecode='vpi=full' --test-talos-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph{vpi=full},talos-sim-verify-compute-graph,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LLVM
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @frames {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32
    simulation.code_unit.decl 44 in 1 initial hierarchy "top.initial"
    simulation.code_unit.decl 45 in 1 function hierarchy "top.worker"
    simulation.statement.decl 11 in 44 scope 1 type 19
    simulation.statement.decl 12 in 45 scope 1 type 33 name "block" {is_scope}
    simulation.statement.decl 13 in 45 scope 1 type 38 parent 12
    simulation.statement_site.decl 1200 on 12 phase 0
    simulation.vpi_call.decl @call id 20 in 44 scope 1 type 19 name "worker" statement 11 within 11 callee 45
    simulation.func @initial(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 44 : i64} {
      %root = simulation.vpi.frame.enter %ctx unit 44
      simulation.vpi.frame.checkpoint %ctx statement 11
      %ticket = simulation.vpi.call.prepare %ctx from @call
      simulation.call @worker(%ctx) : (!simulation.context) -> ()
      simulation.vpi.call.finish %ctx, %ticket
      simulation.vpi.frame.leave %ctx, %root
      simulation.return
    }
    simulation.func private @worker(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 45 : i64} {
      %function = simulation.vpi.frame.enter_prepared %ctx unit 45
      %block = simulation.vpi.frame.enter %ctx unit 45 within 12
      %value = arith.constant 23 : i32
      %reference = simulation.ref.alloc %value : i32 -> !simulation.ref<i32>
      simulation.vpi.frame.checkpoint %ctx statement 13
      simulation.vpi.frame.leave %ctx, %block observing (%reference : !simulation.ref<i32>)
      simulation.vpi.frame.leave %ctx, %function
      simulation.return
    }
  }
}
// BYTECODE-DAG: id=0x00011010 inputs=1 outputs=1 flags=0
// BYTECODE-DAG: id=0x00011011 inputs=2 outputs=1 flags=0
// BYTECODE-DAG: id=0x00011012 inputs=3 outputs=1 flags=0
// BYTECODE-DAG: id=0x00011013 inputs=1 outputs=0 flags=0
// BYTECODE-DAG: id=0x00011014 inputs=1 outputs=0 flags=0
// BYTECODE-DAG: id=0x00011015 inputs=1 outputs=0 flags=0
// LLVM-DAG: llvm.call @talos_rt_v1_vpi_call_prepare
// LLVM-DAG: llvm.call @talos_rt_v1_vpi_frame_enter_prepared
// LLVM-DAG: llvm.call @talos_rt_v1_vpi_frame_enter
// LLVM-DAG: llvm.call @talos_rt_v1_vpi_frame_checkpoint
// LLVM-DAG: llvm.call @talos_rt_v1_vpi_frame_leave
// LLVM-DAG: llvm.call @talos_rt_v1_vpi_call_finish
// LLVM-NOT: simulation.vpi.

// INLINE: simulation.call @worker
// INLINE: simulation.func private @worker
// INLINE: simulation.vpi.frame.enter_prepared
