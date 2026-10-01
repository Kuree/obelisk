// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @block_events {
    simulation.scope.decl 0 hierarchy "block_events"
    simulation.covergroup.decl @cg schema 1
    simulation.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.code_unit.decl 1 in 0 observer
      hierarchy "block_events.sample"
    simulation.code_unit.decl 2 in 0 initial
      hierarchy "block_events.run"
    simulation.code_unit.decl 3 in 0 initial
      hierarchy "block_events.run_receiver_free"

    simulation.func private @sample(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %handle: !simulation.covergroup_handle<@cg>
          {simulation.capture_kind = 2 : i32},
        %receiver: !simulation.class_handle<@Object>
          {simulation.capture_kind = 1 : i32}) -> i1
        attributes {
          code_unit_id = 1 : i64, entry_kind = 14 : i32, internal,
          simulation.covergroup_block_event_sample_evaluator,
          schedule.observer_width = 1 : i32,
          schedule.observer_four_state = false,
          simulation.this_argument = 2 : i32
        } {
      %true = arith.constant true
      simulation.return %true : i1
    }

    simulation.func @run(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %handle = simulation.covergroup.null :
        !simulation.covergroup_handle<@cg>
      %receiver = simulation.class.null :
        !simulation.class_handle<@Object>
      %sampler = simulation.observer.bind @sample
        values(%handle, %receiver : !simulation.covergroup_handle<@cg>,
               !simulation.class_handle<@Object>) captures 2 :
        !simulation.observer<i1>
      simulation.covergroup.block_event.register
          %ctx, %handle, %receiver, %sampler {
        target_ids = array<i64: 101, 102>, event_kinds = array<i32: 0, 1>
      } : (!simulation.context, !simulation.covergroup_handle<@cg>,
           !simulation.class_handle<@Object>, !simulation.observer<i1>) -> ()
      simulation.covergroup.block_event.fire %ctx, %receiver {
        target_id = 101 : i64, event_kind = #simulation.block_event_kind<begin>
      } : (!simulation.context, !simulation.class_handle<@Object>) -> ()
      simulation.return
    }

    simulation.func @run_receiver_free(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      simulation.covergroup.block_event.fire %ctx {
        target_id = 102 : i64, event_kind = #simulation.block_event_kind<end>
      } : (!simulation.context) -> ()
      simulation.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_covergroup_block_event_register(!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, i32, !llvm.ptr, !llvm.ptr, i32) -> i32
// NATIVE-DAG: llvm.func @obelisk_rt_v1_covergroup_block_event_fire(!llvm.ptr, i64, i32, !llvm.ptr) -> i32
// NATIVE-LABEL: llvm.func @run(
// NATIVE: llvm.mlir.constant(101 : i64)
// NATIVE: llvm.mlir.constant(102 : i64)
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_block_event_register
// NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_covergroup_block_event_fire
// NATIVE-NOT: simulation.covergroup.block_event

// BYTECODE: intrinsic {{.*}}id=0x00010474 inputs=11 outputs=0 flags=0
// BYTECODE: intrinsic {{.*}}id=0x00010475 inputs=3 outputs=0 flags=0
// BYTECODE-COUNT-2: site {{.*}}id=0x00010475
