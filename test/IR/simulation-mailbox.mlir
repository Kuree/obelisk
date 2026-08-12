// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @mailbox {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.mailbox"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %bound: i64 {obelisk_sim.capture_kind = 1 : i32},
        %message: !obelisk_sim.string {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // CHECK: %[[MAILBOX:.*]] = obelisk_sim.mailbox.create
      %mailbox = "obelisk_sim.mailbox.create"(%bound) {
        alignment = 8 : i64,
        bit_width = 0 : i64,
        element_flags = 0 : i32,
        element_kind = 5 : i32,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 8 : i64
      } :
        (i64) -> !obelisk_sim.mailbox<!obelisk_sim.string>
      // CHECK: obelisk_sim.mailbox.try_put
      %put = "obelisk_sim.mailbox.try_put"(%mailbox, %message) :
        (!obelisk_sim.mailbox<!obelisk_sim.string>, !obelisk_sim.string) -> i1
      // CHECK: obelisk_sim.mailbox.try_peek
      %peek_ok, %peek = "obelisk_sim.mailbox.try_peek"(%mailbox) :
        (!obelisk_sim.mailbox<!obelisk_sim.string>) ->
        (i1, !obelisk_sim.string)
      // CHECK: obelisk_sim.mailbox.num
      %count = "obelisk_sim.mailbox.num"(%mailbox) :
        (!obelisk_sim.mailbox<!obelisk_sim.string>) -> i32
      // CHECK: obelisk_sim.mailbox.try_get
      %get_ok, %get = "obelisk_sim.mailbox.try_get"(%mailbox) :
        (!obelisk_sim.mailbox<!obelisk_sim.string>) ->
        (i1, !obelisk_sim.string)
      obelisk_sim.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_mailbox_create_typed
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_try_put
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_try_peek
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_num
// NATIVE: llvm.call @obelisk_rt_v1_mailbox_try_get
