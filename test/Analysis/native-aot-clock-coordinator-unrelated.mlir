// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// An unrelated suspension with managed live state cannot inherit
// forced-hybrid admission from a coordinator elsewhere in the design.
// CHECK: native-aot eligible=true fully=false{{.*}}forced_hybrid=false
// CHECK: bytecode @unrelated
// CHECK: reason suspension retains managed state

module {
  simulation.design @unrelated {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "coordinator"
    simulation.code_unit.decl 3 in 0 always hierarchy "unrelated"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %coordinator = simulation.spawn @coordinator(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      %unrelated = simulation.spawn @unrelated(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @coordinator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.multiclock_sequence_coordinator} {
      cf.br ^wait
    ^wait:
      simulation.suspend.clock_set %clock conditions 0 edges [1]
          indices [-1] site 17 to ^resume :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      cf.br ^wait
    }

    simulation.func @unrelated(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %text = simulation.string.literal "unrelated"
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%text : !simulation.string)
    ^resume(%saved: !simulation.string):
      %length = "simulation.string.length"(%saved) : (!simulation.string) -> i64
      cf.br ^wait
    }
  }
}
