// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // The startup wildcard body writes and watches the same storage in separate
  // fragments of one process.  Its own write precedes the terminal wait and
  // cannot activate it, but a write from another process must retain its
  // sensitivity edge.
  // CHECK-LABEL: simulation.design @startup_wildcard_wait attributes {compute_graph = #schedule.graph<
  // CHECK-SAME: #schedule.fragment<id = [[PRODUCER:[0-9]+]], function = @producer, block = 0
  // CHECK-SAME: #schedule.fragment<id = [[WAIT:[0-9]+]], function = @wildcard, block = 1
  // CHECK-SAME: effect = watch
  // CHECK-SAME: #schedule.fragment<id = [[SELF:[0-9]+]], function = @wildcard, block = 2
  // CHECK-SAME: effect = write
  // CHECK-SAME: #schedule.edge<source = [[PRODUCER]], target = [[WAIT]], kind = sensitivity
  // CHECK-NOT: #schedule.edge<source = [[SELF]], target = [[WAIT]], kind = sensitivity
  // CHECK-SAME: regions =
  // The native wait template retains SUPPRESS_ACTIVE_SELF in the header flags.
  // NATIVE-LABEL: llvm.mlir.global internal constant @wildcard.__obelisk_table.waits
  // NATIVE: %[[FLAGS:.*]] = llvm.mlir.constant(4 : i32) : i32
  // NATIVE-NEXT: llvm.insertvalue %[[FLAGS]], %{{.*}}[2]
  // The bytecode image preserves the same wait header: version, CHANGE kind,
  // flags, and watcher count.
  // BYTECODE: obelisk.bytecode.image = array<i8: {{.*}}1, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0
  simulation.design @startup_wildcard_wait {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "startup_wildcard_wait.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "startup_wildcard_wait.wildcard"
    simulation.code_unit.decl 3 in 0 initial
        hierarchy "startup_wildcard_wait.producer"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %wildcard = simulation.spawn @wildcard(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %producer = simulation.spawn @producer(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @wildcard(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %state to ^done
          {schedule.top_level_wildcard_wait} :
          !simulation.ref<!simulation.logic<1>>
    ^done:
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %state : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @producer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
          {simulation.capture_kind = 3 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.ref.store %one to %state : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      simulation.return
    }
  }
}
