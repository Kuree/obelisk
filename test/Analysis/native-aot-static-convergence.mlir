// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A convergence SCC driven by fixed descriptor-backed suspend.any watches is
// a native dirty-set fixpoint. It must not be routed through bytecode.
// CHECK: native-aot eligible=true fully=true
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @settle
// CHECK-NOT: bytecode
// CHECK-NOT: reason

module {
  obelisk_sim.design @static_convergence {
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "settle"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %process = obelisk_sim.spawn @settle(%ctx, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @settle(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %state :
          !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.any %state edges [0] to ^resume :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
    ^resume:
      %next = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %next to %state :
          !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.any %state edges [0] to ^resume :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }
  }
}
