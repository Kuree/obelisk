// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=AOT
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' \
// RUN:   | FileCheck %s --check-prefix=GRAPH

// A compiled Clause 31.7 condition remains an actor-local clock wait. Its
// source is a data read for ordering, never an independent observer wakeup.
// AOT: native-aot eligible=true fully=false{{.*}}forced_hybrid=true
// AOT-NEXT: actor 0 @root
// AOT-NEXT: bytecode @timing_check bb1
// AOT: reason clock cohort wait requires runtime ordering
// GRAPH: effect = read
// GRAPH: simulation.suspend.clock_set
// GRAPH-SAME: !simulation.observer<i1>
// GRAPH-NOT: simulation.suspend.observe

module {
  simulation.design @timing_check_computed {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "timing_check"
    simulation.code_unit.decl 3 in 0 observer hierarchy "condition"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %source = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<1>>
      %process = simulation.spawn @timing_check(%ctx, %clock, %source) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @condition(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = simulation.ref.load %source :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %truth = simulation.logic.is_true %value : !simulation.logic<1>
      simulation.return %truth : i1
    }

    simulation.func private @timing_check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %source: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.timing_check_coordinator} {
      %bound = simulation.observer.bind @condition
          values(%source, %source : !simulation.ref<!simulation.logic<1>>,
                 !simulation.ref<!simulation.logic<1>>) captures 1 :
          !simulation.observer<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.clock_set %clock, %bound conditions 1 edges [1]
          indices [0] site 23 to ^resume :
          !simulation.ref<!simulation.logic<1>>, !simulation.observer<i1>
    ^resume:
      cf.br ^wait
    }
  }
}
