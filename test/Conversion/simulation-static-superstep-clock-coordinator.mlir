// RUN: %split-file %s %t
// RUN: obelisk-opt %t/positive.mlir \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-plan-static-superstep))' \
// RUN:   | FileCheck %s --check-prefix=POSITIVE
// RUN: obelisk-opt %t/residual-cycle.mlir \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-plan-static-superstep{missed-remarks=true}))' \
// RUN:   > %t/residual-cycle.out 2> %t/residual-cycle.err
// RUN: FileCheck %s --check-prefix=NEGATIVE < %t/residual-cycle.out
// RUN: FileCheck %s --check-prefix=NEGATIVE-REMARK < %t/residual-cycle.err

// A runtime-owned Clause 31 coordinator may close an Observed scheduling SCC
// around an ordinary actor. Removing that exact cold actor leaves no procedural
// cycle, so the ordinary actor remains in the certified static island.

//--- positive.mlir
module {
  // POSITIVE: obelisk_sim.design @mixed attributes {
  // POSITIVE-SAME: obelisk_sim.static_superstep = #obelisk_sim.static_superstep<version = 1
  // POSITIVE-SAME: actors = [@root, @ordinary]
  obelisk_sim.design @mixed attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @coordinator, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 2, function = @coordinator, block = 1,
          region = observed, action = suspend_any, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 3, function = @ordinary, block = 0,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #obelisk_sim.edge<source = 0, target = 1, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 3, kind = spawn>,
        #obelisk_sim.edge<source = 1, target = 2, kind = process_order>,
        #obelisk_sim.edge<source = 2, target = 3, kind = process_order>,
        #obelisk_sim.edge<source = 3, target = 2, kind = process_order>
      ],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = [
          #obelisk_sim.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [2, 3], schedule = control_loop,
            feedback = []>]>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "coordinator"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "ordinary"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %coordinator = obelisk_sim.spawn @coordinator(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %ordinary = obelisk_sim.spawn @ordinary(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @coordinator(
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
          indices [-1] site 23 to ^wait :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func @ordinary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      obelisk_sim.return
    }
  }
}

// A cold coordinator must not conceal a residual ordinary ProcessOrder cycle.

//--- residual-cycle.mlir
module {
  // NEGATIVE: obelisk_sim.design @residual_cycle attributes {
  // NEGATIVE-NOT: obelisk_sim.static_superstep
  // NEGATIVE-REMARK: remark: static superstep not planned: control-loop compute group
  obelisk_sim.design @residual_cycle attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @root, block = 0,
          region = active, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @coordinator, block = 0,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 2, function = @coordinator, block = 1,
          region = observed, action = suspend_any, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 3, function = @ordinary_a, block = 0,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 4, function = @ordinary_b, block = 0,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #obelisk_sim.edge<source = 0, target = 1, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 3, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 4, kind = spawn>,
        #obelisk_sim.edge<source = 1, target = 2, kind = process_order>,
        #obelisk_sim.edge<source = 2, target = 3, kind = process_order>,
        #obelisk_sim.edge<source = 3, target = 4, kind = process_order>,
        #obelisk_sim.edge<source = 4, target = 3, kind = process_order>,
        #obelisk_sim.edge<source = 4, target = 2, kind = process_order>
      ],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = [
          #obelisk_sim.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [2, 3, 4], schedule = control_loop,
            feedback = []>]>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "coordinator"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "ordinary_a"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "ordinary_b"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %coordinator = obelisk_sim.spawn @coordinator(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      %a = obelisk_sim.spawn @ordinary_a(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      %b = obelisk_sim.spawn @ordinary_b(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @coordinator(
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
          indices [-1] site 23 to ^wait :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func @ordinary_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @ordinary_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64,
                    domain = 0 : i32, home_region = 8 : i32} {
      obelisk_sim.return
    }
  }
}
