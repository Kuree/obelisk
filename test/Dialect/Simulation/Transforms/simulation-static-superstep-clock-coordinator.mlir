// RUN: %split-file %s %t
// RUN: obelisk-opt %t/positive.mlir \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-plan-static-superstep))' \
// RUN:   | FileCheck %s --check-prefix=POSITIVE
// RUN: obelisk-opt %t/residual-cycle.mlir \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-plan-static-superstep{missed-remarks=true}))' \
// RUN:   > %t/residual-cycle.out 2> %t/residual-cycle.err
// RUN: FileCheck %s --check-prefix=PARTIAL < %t/residual-cycle.out

// A runtime-owned Clause 31 coordinator may close an Observed scheduling SCC
// around an ordinary actor. Removing that exact cold actor leaves no procedural
// cycle, so the ordinary actor remains in the certified static island.

//--- positive.mlir
module {
  // POSITIVE: simulation.design @mixed attributes {
  // POSITIVE-SAME: schedule.static_superstep = #schedule.static_superstep<version = 1
  // POSITIVE-SAME: actors = [@root, @ordinary]
  simulation.design @mixed attributes {
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
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = [
          #schedule.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [2, 3], schedule = control_loop,
            feedback = []>]>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
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

// A residual ordinary ProcessOrder cycle remains bytecode-owned while the
// other admitted work keeps its static plan.

//--- residual-cycle.mlir
module {
  // PARTIAL: simulation.design @residual_cycle attributes {
  // PARTIAL-SAME: schedule.static_superstep = #schedule.static_superstep<version = 1
  // PARTIAL-SAME: actors = [@root, @ordinary_a, @ordinary_b]
  simulation.design @residual_cycle attributes {
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
        #schedule.fragment<id = 3, function = @ordinary_a, block = 0,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 4, function = @ordinary_b, block = 0,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #schedule.edge<source = 0, target = 1, kind = spawn>,
        #schedule.edge<source = 0, target = 3, kind = spawn>,
        #schedule.edge<source = 0, target = 4, kind = spawn>,
        #schedule.edge<source = 1, target = 2, kind = process_order>,
        #schedule.edge<source = 2, target = 3, kind = process_order>,
        #schedule.edge<source = 3, target = 4, kind = process_order>,
        #schedule.edge<source = 4, target = 3, kind = process_order>,
        #schedule.edge<source = 4, target = 2, kind = process_order>
      ],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = [
          #schedule.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #schedule.group<fragments = [2, 3, 4], schedule = control_loop,
            feedback = []>]>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "coordinator"
    simulation.code_unit.decl 3 in 0 initial hierarchy "ordinary_a"
    simulation.code_unit.decl 4 in 0 initial hierarchy "ordinary_b"

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %coordinator = simulation.spawn @coordinator(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      %a = simulation.spawn @ordinary_a(%ctx) :
          !simulation.context -> !simulation.process
      %b = simulation.spawn @ordinary_b(%ctx) :
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

    simulation.func @ordinary_a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      simulation.return
    }

    simulation.func @ordinary_b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      simulation.return
    }
  }
}
