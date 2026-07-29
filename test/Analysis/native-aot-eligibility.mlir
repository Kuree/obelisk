// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=ELIGIBLE
// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{workers=2},obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=MULTI-WORKER

// ELIGIBLE: native-aot eligible=true fully=true
// ELIGIBLE-NEXT: actor 0 @root
// ELIGIBLE-NEXT: actor 1 @z_always
// ELIGIBLE-NEXT: actor 2 @a_initial
// ELIGIBLE-NOT: reason

// MULTI-WORKER: native-aot eligible=false fully=false
// MULTI-WORKER-NEXT: reason AOT scheduling requires one worker
// MULTI-WORKER-NOT: actor

module {
  obelisk_sim.design @eligible {
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "a_initial"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "z_always"
    obelisk_sim.scope.decl 0

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %always = obelisk_sim.spawn @z_always(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %initial = obelisk_sim.spawn @a_initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @a_initial(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.return
    }

    obelisk_sim.func @z_always(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.return
    }
  }
}
