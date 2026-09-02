// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A compiler-generated persistent monitor spawned unconditionally by a
// once-spawned initial block is a statically bounded actor.
// CHECK: native-aot eligible=true fully=true selected=true
// CHECK: actor 2 @monitor
// CHECK-NOT: bytecode
// CHECK-NOT: reason

module {
  obelisk_sim.design @static_monitor {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "initial"
    obelisk_sim.code_unit.decl 3 in 0 fork hierarchy "monitor" {internal}

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @initial(%ctx, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %monitor = obelisk_sim.spawn @monitor(%ctx, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      obelisk_sim.monitor.register %monitor
      obelisk_sim.return
    }

    obelisk_sim.func private @monitor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 13 : i32, code_unit_id = 3 : i64,
                    home_region = 16 : i32, domain = 0 : i32, internal,
                    obelisk_sim.persistent_monitor} {
      cf.br ^dispatch
    ^dispatch:
      %current = obelisk_sim.monitor.current
      cf.cond_br %current, ^body, ^stale
    ^body:
      obelisk_sim.suspend.change %state to ^dispatch :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^stale:
      obelisk_sim.return
    }
  }
}
