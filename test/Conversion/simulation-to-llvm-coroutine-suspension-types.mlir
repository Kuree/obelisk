// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @suspension_types {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 observer hierarchy "suspension_types.evaluate"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "suspension_types.process"

    obelisk_sim.func private @evaluate(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32}) -> i1
        attributes {
          code_unit_id = 1 : i64, entry_kind = 14 : i32,
          obelisk_sim.observer_four_state = false,
          obelisk_sim.observer_width = 1 : i32
        } {
      %false = arith.constant false
      obelisk_sim.return %false : i1
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<17>>
            {obelisk_sim.capture_kind = 1 : i32},
        %net: !obelisk_sim.net<i9>
            {obelisk_sim.capture_kind = 1 : i32},
        %event: !obelisk_sim.event
            {obelisk_sim.capture_kind = 1 : i32},
        %child: !obelisk_sim.process
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %observer = obelisk_sim.observer.bind @evaluate values(
          %ref, %net, %event :
          !obelisk_sim.ref<!obelisk_sim.logic<17>>,
          !obelisk_sim.net<i9>, !obelisk_sim.event) captures 0 :
          !obelisk_sim.observer<i1>
      %false = arith.constant false
      obelisk_sim.suspend.observe %observer, %false conditions 0
          edges [0] indices [-1] to ^change :
          !obelisk_sim.observer<i1>, i1
    ^change:
      obelisk_sim.suspend.change %ref to ^edge :
          !obelisk_sim.ref<!obelisk_sim.logic<17>>
    ^edge:
      obelisk_sim.suspend.edge posedge %net to ^any : !obelisk_sim.net<i9>
    ^any:
      obelisk_sim.suspend.any %ref, %net edges [0, 1] to ^event_wait :
          !obelisk_sim.ref<!obelisk_sim.logic<17>>, !obelisk_sim.net<i9>
    ^event_wait:
      obelisk_sim.suspend.event %event to ^await
    ^await:
      obelisk_sim.suspend.await %child to ^join
    ^join:
      obelisk_sim.suspend.join all %child processes 1 to ^children :
          !obelisk_sim.process
    ^children:
      obelisk_sim.suspend.children to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @process.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, 2, 3, 4, 5, 6, 7, 8>
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.constant(17 : i32)
// CHECK: llvm.mlir.constant(9 : i32)
// CHECK: llvm.mlir.constant(2 : i32)
// CHECK: llvm.mlir.constant(17 : i32)
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK-NOT: obelisk_sim.observer
// CHECK-NOT: obelisk_sim.suspend
