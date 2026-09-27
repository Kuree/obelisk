// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A compiler-generated persistent monitor spawned unconditionally by a
// once-spawned initial block is a statically bounded actor.
// CHECK: native-aot eligible=true fully=true selected=true
// CHECK: actor 2 @monitor
// CHECK-NOT: bytecode
// CHECK-NOT: reason

module {
  simulation.design @static_monitor {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "initial"
    simulation.code_unit.decl 3 in 0 fork hierarchy "monitor" {internal}

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %process = simulation.spawn @initial(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      simulation.return
    }

    simulation.func @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %monitor = simulation.spawn @monitor(%ctx, %state) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      simulation.monitor.register %monitor
      simulation.return
    }

    simulation.func private @monitor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 13 : i32, code_unit_id = 3 : i64,
                    home_region = 16 : i32, domain = 0 : i32, internal,
                    simulation.persistent_monitor} {
      cf.br ^dispatch
    ^dispatch:
      %current = simulation.monitor.current
      cf.cond_br %current, ^body, ^stale
    ^body:
      simulation.suspend.change %state to ^dispatch :
          !simulation.ref<!simulation.logic<1>>
    ^stale:
      simulation.return
    }
  }
}
