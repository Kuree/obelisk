// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @mailbox {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.mailbox"

    simulation.func @exercise(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %bound: i64 {simulation.capture_kind = 1 : i32},
        %message: !simulation.string {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // CHECK: %[[MAILBOX:.*]] = simulation.mailbox.create
      %mailbox = simulation.mailbox.create %bound {
        alignment = 8 : i64,
        bit_width = 0 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<string>,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 8 : i64
      } :
        (i64) -> !simulation.mailbox<!simulation.string>
      %array = simulation.container.create %bound {
        alignment = 4 : i64,
        bit_width = 32 : i64,
        bound = 0 : i64,
        container_kind = #simulation.container_kind<dynamic_array>,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<bits>,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 42 : i64,
        value_size = 4 : i64
      } : (i64) -> !simulation.dynamic_array<i32>
      // CHECK: simulation.box.pack
      %box = simulation.box.pack %array :
        (!simulation.dynamic_array<i32>) -> !simulation.box
      // CHECK: simulation.box.is_type
      %matches = simulation.box.is_type %box type_id 42 : !simulation.box
      // CHECK: simulation.box.cast
      %unboxed = simulation.box.cast %box :
        (!simulation.box) -> !simulation.dynamic_array<i32>
      // CHECK: simulation.mailbox.try_put
      %put = simulation.mailbox.try_put %mailbox, %message :
        (!simulation.mailbox<!simulation.string>, !simulation.string) -> i1
      // CHECK: simulation.mailbox.try_peek
      %peek_ok, %peek = simulation.mailbox.try_peek %mailbox :
        (!simulation.mailbox<!simulation.string>) ->
        (i1, !simulation.string)
      // CHECK: simulation.mailbox.num
      %count = simulation.mailbox.num %mailbox :
        (!simulation.mailbox<!simulation.string>) -> i32
      // CHECK: simulation.mailbox.try_get
      %get_ok, %get = simulation.mailbox.try_get %mailbox :
        (!simulation.mailbox<!simulation.string>) ->
        (i1, !simulation.string)
      // CHECK: simulation.suspend.mailbox %[[MAILBOX]] not_empty
      simulation.suspend.mailbox %mailbox not_empty to ^not_full(
        %mailbox : !simulation.mailbox<!simulation.string>) :
        !simulation.mailbox<!simulation.string>
    ^not_full(%live: !simulation.mailbox<!simulation.string>):
      // CHECK: simulation.suspend.mailbox %{{.*}} not_full
      simulation.suspend.mailbox %live not_full to ^done(
        %live : !simulation.mailbox<!simulation.string>) :
        !simulation.mailbox<!simulation.string>
    ^done(%still_live: !simulation.mailbox<!simulation.string>):
      simulation.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_mailbox_create_typed
// NATIVE: llvm.call @obelisk_rt_v1_box_is_type
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_try_put
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_try_peek
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_num
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_try_get
// NATIVE: llvm.mlir.constant(11 : i32)
// BYTECODE: obelisk.bytecode.image
