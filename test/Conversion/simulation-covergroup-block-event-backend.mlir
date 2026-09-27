// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @block_events {
    obelisk_sim.scope.decl 0 hierarchy "block_events"
    obelisk_sim.covergroup.decl @cg schema 1
    obelisk_sim.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.code_unit.decl 1 in 0 observer
      hierarchy "block_events.sample"
    obelisk_sim.code_unit.decl 2 in 0 initial
      hierarchy "block_events.run"
    obelisk_sim.code_unit.decl 3 in 0 initial
      hierarchy "block_events.run_receiver_free"

    obelisk_sim.func private @sample(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %handle: !obelisk_sim.covergroup_handle<@cg>
          {obelisk_sim.capture_kind = 2 : i32},
        %receiver: !obelisk_sim.class_handle<@Object>
          {obelisk_sim.capture_kind = 1 : i32}) -> i1
        attributes {
          code_unit_id = 1 : i64, entry_kind = 14 : i32, internal,
          obelisk_sim.covergroup_block_event_sample_evaluator,
          schedule.observer_width = 1 : i32,
          schedule.observer_four_state = false,
          obelisk_sim.this_argument = 2 : i32
        } {
      %true = arith.constant true
      obelisk_sim.return %true : i1
    }

    obelisk_sim.func @run(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %handle = obelisk_sim.covergroup.null :
        !obelisk_sim.covergroup_handle<@cg>
      %receiver = obelisk_sim.class.null :
        !obelisk_sim.class_handle<@Object>
      %sampler = obelisk_sim.observer.bind @sample
        values(%handle, %receiver : !obelisk_sim.covergroup_handle<@cg>,
               !obelisk_sim.class_handle<@Object>) captures 2 :
        !obelisk_sim.observer<i1>
      "obelisk_sim.covergroup.block_event.register"(
          %ctx, %handle, %receiver, %sampler) {
        target_ids = array<i64: 101, 102>, event_kinds = array<i32: 0, 1>
      } : (!obelisk_sim.context, !obelisk_sim.covergroup_handle<@cg>,
           !obelisk_sim.class_handle<@Object>, !obelisk_sim.observer<i1>) -> ()
      "obelisk_sim.covergroup.block_event.fire"(%ctx, %receiver) {
        target_id = 101 : i64, event_kind = 0 : i32
      } : (!obelisk_sim.context, !obelisk_sim.class_handle<@Object>) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @run_receiver_free(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      "obelisk_sim.covergroup.block_event.fire"(%ctx) {
        target_id = 102 : i64, event_kind = 1 : i32
      } : (!obelisk_sim.context) -> ()
      obelisk_sim.return
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
// NATIVE-NOT: obelisk_sim.covergroup.block_event

// BYTECODE: intrinsic {{.*}}id=0x00010474 inputs=11 outputs=0 flags=0
// BYTECODE: intrinsic {{.*}}id=0x00010475 inputs=3 outputs=0 flags=0
// BYTECODE-COUNT-2: site {{.*}}id=0x00010475
