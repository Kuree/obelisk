// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A Clause 31 coordinator joins an ordinary actor in one control-loop SCC.
// The ordinary actor's only process-order cycle is a for-loop whose latch the
// bounded-loop marker proved to run finitely often (IEEE 1800-2023 12.7.1), so
// it is not the zero-delay loop hazard of 12.7.6. ComputeGraph.cpp drops
// that backedge before classifying groups, so the coordinator isolation and
// the procedural-cycle residue must drop it as well: removing the cold
// coordinator leaves an acyclic actor, and only the coordinator becomes a
// bytecode island.
// CHECK: native-aot eligible=true fully=false{{.*}}forced_hybrid=true
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @ordinary
// CHECK-NEXT: bytecode @coordinator bb1
// CHECK-NOT: bytecode @ordinary
// CHECK: reason control-loop group requires bytecode scheduling

module {
  obelisk_sim.design @bounded attributes {
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
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 4, function = @ordinary, block = 1,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 5, function = @ordinary, block = 2,
          region = observed, action = continue, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 6, function = @ordinary, block = 3,
          region = observed, action = terminate, tier = native, cost = 1,
          lane = 0, twoState = true, effects = []>
      ],
      edges = [
        #obelisk_sim.edge<source = 0, target = 1, kind = spawn>,
        #obelisk_sim.edge<source = 0, target = 3, kind = spawn>,
        #obelisk_sim.edge<source = 1, target = 2, kind = process_order>,
        #obelisk_sim.edge<source = 2, target = 3, kind = process_order>,
        #obelisk_sim.edge<source = 3, target = 4, kind = process_order>,
        #obelisk_sim.edge<source = 4, target = 5, kind = process_order>,
        #obelisk_sim.edge<source = 4, target = 6, kind = process_order>,
        #obelisk_sim.edge<source = 5, target = 4, kind = process_order>,
        #obelisk_sim.edge<source = 6, target = 2, kind = process_order>
      ],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>
        ]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = [
          #obelisk_sim.group<fragments = [1], schedule = acyclic,
            feedback = []>,
          #obelisk_sim.group<fragments = [2, 3, 4, 5, 6],
            schedule = control_loop, feedback = []>
        ]>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>
      ]>
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
      %zero = arith.constant 0 : i32
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %limit = arith.constant 4 : i32
      %test = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %test, ^body, ^exit {obelisk_sim.bounded_loop_header}
    ^body:
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32) {obelisk_sim.bounded_loop_latch}
    ^exit:
      obelisk_sim.return
    }
  }
}
