// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // A settling publication targets the wait fragment, but wakes the wait's
  // continuation. Keep that resumed procedural body after its producer even
  // when symbol order assigns the body a lower fragment ID.
  // CHECK-LABEL: simulation.design @procedural_observer_order attributes {compute_graph = #schedule.graph<
  // CHECK-SAME: #schedule.fragment<id = [[WAIT:[0-9]+]], function = @a_observer, block = 0
  // CHECK-SAME: #schedule.fragment<id = [[BODY:[0-9]+]], function = @a_observer, block = 1
  // CHECK-SAME: #schedule.fragment<id = [[PRODUCER:[0-9]+]], function = @z_producer, block = 0
  // CHECK-SAME: #schedule.edge<source = [[PRODUCER]], target = [[WAIT]], kind = sensitivity
  // CHECK-SAME: regions = [#schedule.region<kind = active, groups = [
  // CHECK-SAME: #schedule.group<fragments = {{\[}}[[PRODUCER]]{{\]}}
  // CHECK-SAME: #schedule.group<fragments = {{\[}}[[WAIT]]{{\]}}
  // CHECK-SAME: #schedule.group<fragments = {{\[}}[[BODY]]{{\]}}
  // Startup infrastructure carries bit 5 in the native scheduler flags.
  // NATIVE-LABEL: llvm.func @z_producer.__obelisk_spawn
  // NATIVE: %[[STARTUP:.*]] = llvm.mlir.constant(32 : i32)
  // NATIVE: llvm.call @obelisk_rt_v1_scheduler_add_planned
  // NATIVE-SAME: %[[STARTUP]]
  // The bytecode SPAWN signature stores the same classification in bit 31 of
  // its flags word. The target is function index 2 and has two captures.
  // BYTECODE: obelisk.bytecode.image = array<i8: {{.*}}0, 2, 1, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, -128
  simulation.design @procedural_observer_order {
    simulation.scope.decl 0
    simulation.code_unit.decl 9800000 in 0 root_initializer
        hierarchy "procedural_observer_order.root"
    simulation.code_unit.decl 9800001 in 0 always
        hierarchy "procedural_observer_order.a_observer"
    simulation.code_unit.decl 9800002 in 0 continuous
        hierarchy "procedural_observer_order.z_producer"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9800000 : i64} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %producer = simulation.spawn @z_producer(%ctx, %driver) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @a_observer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<1>>
          {simulation.capture_kind = 4 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9800001 : i64} {
      simulation.suspend.change %net to ^body :
          !simulation.net<!simulation.logic<1>>
    ^body:
      %value = simulation.net.read %net :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.return
    }

    simulation.func @z_producer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<1>>
          {simulation.capture_kind = 5 : i32,
           simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9800002 : i64} {
      %one = simulation.logic.constant true, false :
          !simulation.logic<1>
      simulation.driver.drive %driver = %one :
          !simulation.driver<!simulation.logic<1>>,
          !simulation.logic<1>
      simulation.return
    }
  }
}
