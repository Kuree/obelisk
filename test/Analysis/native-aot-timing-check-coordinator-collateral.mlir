// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A Clause 31 coordinator can join ordinary actors in one control-loop SCC.
// Removing the proven cold actor leaves an acyclic induced process-order
// graph, so only that coordinator becomes a bytecode island.
// CHECK: native-aot eligible=true fully=false{{.*}}forced_hybrid=true
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @ordinary
// CHECK-NEXT: bytecode @coordinator bb1
// CHECK: reason clock cohort wait requires runtime ordering
// CHECK: reason control-loop group requires bytecode scheduling

module {
  simulation.design @collateral attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @coordinator, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 2, function = @coordinator, block = 1,
          region = observed, action = suspend_any, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 3, function = @ordinary, block = 0,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #schedule.edge<source = 0, target = 1, kind = spawn>,
        #schedule.edge<source = 0, target = 3, kind = spawn>,
        #schedule.edge<source = 1, target = 2, kind = process_order>,
        #schedule.edge<source = 2, target = 3, kind = process_order>,
        #schedule.edge<source = 3, target = 2, kind = process_order>
      ],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>
        ]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = [
          #schedule.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [2, 3], schedule = control_loop,
            feedback = []>
        ]>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>
      ]>
  } {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "coordinator"
    simulation.code_unit.decl 3 in 0 initial hierarchy "ordinary"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %coordinator = simulation.spawn @coordinator(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      %ordinary = simulation.spawn @ordinary(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @coordinator(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.timing_check_coordinator} {
      cf.br ^wait
    ^wait:
      simulation.suspend.clock_set %clock conditions 0 edges [1]
          indices [-1] site 23 to ^wait :
          !simulation.ref<!simulation.logic<1>>
    }

    simulation.func @ordinary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      simulation.return
    }
  }
}
