// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A private Observed Clause 31 coordinator is an actor-local hybrid island;
// explicit AOT keeps the rest of the design in its generated schedule.
// CHECK: native-aot eligible=true fully=false{{.*}}forced_hybrid=true
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: bytecode @timing_check bb1
// CHECK: reason clock cohort wait requires runtime ordering

module {
  obelisk_sim.design @timing_check {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "timing_check"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @timing_check(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @timing_check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    obelisk_sim.timing_check_coordinator} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.clock_set %clock conditions 0 edges [1]
          indices [-1] site 23 to ^resume :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      cf.br ^wait
    }
  }
}
