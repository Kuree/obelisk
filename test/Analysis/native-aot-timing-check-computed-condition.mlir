// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s --check-prefix=AOT
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' \
// RUN:   | FileCheck %s --check-prefix=GRAPH

// A compiled Clause 31.7 condition remains an actor-local clock wait. Its
// source is a data read for ordering, never an independent observer wakeup.
// AOT: native-aot eligible=true fully=false{{.*}}forced_hybrid=true
// AOT-NEXT: actor 0 @root
// AOT-NEXT: bytecode @timing_check bb1
// AOT: reason clock cohort wait requires runtime ordering
// GRAPH: effect = read
// GRAPH: obelisk_sim.suspend.clock_set
// GRAPH-SAME: !obelisk_sim.observer<i1>
// GRAPH-NOT: obelisk_sim.suspend.observe

module {
  obelisk_sim.design @timing_check_computed {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "timing_check"
    obelisk_sim.code_unit.decl 3 in 0 observer hierarchy "condition"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @timing_check(%ctx, %clock, %source) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @condition(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 2 : i32}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 3 : i64} {
      %value = obelisk_sim.ref.load %source :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %truth = obelisk_sim.logic.is_true %value : !obelisk_sim.logic<1>
      obelisk_sim.return %truth : i1
    }

    obelisk_sim.func private @timing_check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    obelisk_sim.timing_check_coordinator} {
      %bound = obelisk_sim.observer.bind @condition
          values(%source, %source : !obelisk_sim.ref<!obelisk_sim.logic<1>>,
                 !obelisk_sim.ref<!obelisk_sim.logic<1>>) captures 1 :
          !obelisk_sim.observer<i1>
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.clock_set %clock, %bound conditions 1 edges [1]
          indices [0] site 23 to ^resume :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.observer<i1>
    ^resume:
      cf.br ^wait
    }
  }
}
