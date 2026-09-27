// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A convergence SCC driven by fixed descriptor-backed suspend.any watches is
// a native dirty-set fixpoint. It must not be routed through bytecode.
// CHECK: native-aot eligible=true fully=true
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @settle
// CHECK-NOT: bytecode
// CHECK-NOT: reason

module {
  simulation.design @static_convergence {
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "settle"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<8>>
      %process = simulation.spawn @settle(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @settle(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %state :
          !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.any %state edges [0] to ^resume :
          !simulation.ref<!simulation.logic<8>>
    ^resume:
      %next = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %next to %state :
          !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.any %state edges [0] to ^resume :
          !simulation.ref<!simulation.logic<8>>
    }
  }
}
