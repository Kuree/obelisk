// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  obelisk_sim.design @mailbox {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.mailbox"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %bound: i64 {obelisk_sim.capture_kind = 1 : i32},
        %message: !obelisk_sim.string {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      // CHECK: %[[MAILBOX:.*]] = obelisk_sim.mailbox.create
      %mailbox = "obelisk_sim.mailbox.create"(%bound) :
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
