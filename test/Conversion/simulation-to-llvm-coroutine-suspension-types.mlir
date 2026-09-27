// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @suspension_types {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 observer hierarchy "suspension_types.evaluate"
    simulation.code_unit.decl 2 in 0 initial hierarchy "suspension_types.process"

    simulation.func private @evaluate(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %process_ref: !simulation.ref<!simulation.process>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {
          code_unit_id = 1 : i64, entry_kind = 14 : i32,
          schedule.observer_four_state = false,
          schedule.observer_width = 1 : i32
        } {
      %false = arith.constant false
      simulation.return %false : i1
    }

    simulation.func @process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<17>>
            {simulation.capture_kind = 1 : i32},
        %net: !simulation.net<i9>
            {simulation.capture_kind = 1 : i32},
        %driver: !simulation.driver<f64>
            {simulation.capture_kind = 1 : i32},
        %event: !simulation.event
            {simulation.capture_kind = 1 : i32},
        %child: !simulation.process
            {simulation.capture_kind = 1 : i32},
        %process_ref: !simulation.ref<!simulation.process>
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %observer = simulation.observer.bind @evaluate values(
          %process_ref, %process_ref, %ref, %net, %event :
          !simulation.ref<!simulation.process>,
          !simulation.ref<!simulation.process>,
          !simulation.ref<!simulation.logic<17>>,
          !simulation.net<i9>, !simulation.event) captures 1 :
          !simulation.observer<i1>
      %false = arith.constant false
      simulation.suspend.observe %observer, %false conditions 0
          edges [0] indices [-1] to ^change :
          !simulation.observer<i1>, i1
    ^change:
      // IEEE 1800-2017 6.6.7: generated resolution waits observe raw driver
      // contribution changes, including atomic real-valued drivers.
      simulation.suspend.change %driver to ^edge :
          !simulation.driver<f64>
    ^edge:
      simulation.suspend.edge posedge %net to ^any : !simulation.net<i9>
    ^any:
      simulation.suspend.any %ref, %net edges [0, 1] to ^event_wait :
          !simulation.ref<!simulation.logic<17>>, !simulation.net<i9>
    ^event_wait:
      simulation.suspend.event %event to ^await
    ^await:
      simulation.suspend.await %child to ^join
    ^join:
      simulation.suspend.join all %child processes 1 to ^children :
          !simulation.process
    ^children:
      simulation.suspend.children to ^done
    ^done:
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @evaluate.__obelisk_observer(
// CHECK-SAME: %[[CONTEXT:.*]]: !llvm.ptr, %{{.*}}: !llvm.ptr, %{{.*}}: i32,
// CHECK-SAME: %[[VALUE:.*]]: !llvm.ptr, %[[UNKNOWN:.*]]: !llvm.ptr,
// CHECK-SAME: %{{.*}}: i32) -> i32
// CHECK: %[[RESULT:.*]] = llvm.call @evaluate(%[[CONTEXT]], %{{.*}})
// CHECK: llvm.store %[[RESULT]], %[[VALUE]]
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @process.__obelisk_coro_ramp
// CHECK-SAME: obelisk.frame.continuations = array<i32: 0, 1, 2, 3, 4, 5, 6, 7, 8>
// CHECK: llvm.mlir.constant(1 : i64)
// CHECK: llvm.mlir.constant(64 : i32)
// CHECK: llvm.mlir.constant(17 : i32)
// CHECK: llvm.mlir.constant(9 : i32)
// CHECK: llvm.mlir.constant(2 : i32)
// CHECK: llvm.mlir.constant(17 : i32)
// CHECK: llvm.mlir.constant(1 : i32)
// CHECK-NOT: simulation.observer
// CHECK-NOT: simulation.suspend
