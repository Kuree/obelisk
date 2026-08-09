// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // A settling publication targets the wait fragment, but wakes the wait's
  // continuation. Keep that resumed procedural body after its producer even
  // when symbol order assigns the body a lower fragment ID.
  // CHECK-LABEL: obelisk_sim.design @procedural_observer_order attributes {compute_graph = #obelisk_sim.graph<
  // CHECK-SAME: #obelisk_sim.fragment<id = [[WAIT:[0-9]+]], function = @a_observer, block = 0
  // CHECK-SAME: #obelisk_sim.fragment<id = [[BODY:[0-9]+]], function = @a_observer, block = 1
  // CHECK-SAME: #obelisk_sim.fragment<id = [[PRODUCER:[0-9]+]], function = @z_producer, block = 0
  // CHECK-SAME: #obelisk_sim.edge<source = [[PRODUCER]], target = [[WAIT]], kind = sensitivity
  // CHECK-SAME: regions = [#obelisk_sim.region<kind = active, groups = [
  // CHECK-SAME: #obelisk_sim.group<fragments = {{\[}}[[PRODUCER]]{{\]}}
  // CHECK-SAME: #obelisk_sim.group<fragments = {{\[}}[[WAIT]]{{\]}}
  // CHECK-SAME: #obelisk_sim.group<fragments = {{\[}}[[BODY]]{{\]}}
  // Startup infrastructure carries bit 5 in the native scheduler flags.
  // NATIVE-LABEL: llvm.func @z_producer.__obelisk_spawn
  // NATIVE: %[[STARTUP:.*]] = llvm.mlir.constant(32 : i32)
  // NATIVE: llvm.call @obelisk_rt_v1_scheduler_add_planned
  // NATIVE-SAME: %[[STARTUP]]
  // The bytecode SPAWN signature stores the same classification in bit 31 of
  // its flags word. The target is function index 2 and has two captures.
  // BYTECODE: obelisk.bytecode.image = array<i8: {{.*}}0, 2, 1, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, -128
  obelisk_sim.design @procedural_observer_order {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9800000 in 0 root_initializer
        hierarchy "procedural_observer_order.root"
    obelisk_sim.code_unit.decl 9800001 in 0 always
        hierarchy "procedural_observer_order.a_observer"
    obelisk_sim.code_unit.decl 9800002 in 0 continuous
        hierarchy "procedural_observer_order.z_producer"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9800000 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %producer = obelisk_sim.spawn @z_producer(%ctx, %driver) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @a_observer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 4 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9800001 : i64} {
      obelisk_sim.suspend.change %net to ^body :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
    ^body:
      %value = obelisk_sim.net.read %net :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func @z_producer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 5 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9800002 : i64} {
      %one = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
