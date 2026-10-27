// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph))' | FileCheck %s --implicit-check-not='kind = process_order'

// The old representation stored one startup x initial edge per pair, N*N for
// N of each. Only the 2N spawn edges remain. Barriers never escape into graph
// node IDs or groups, and initial entries still follow all startup entries.
// CHECK: #schedule.fragment<id = 32, function = @root
// CHECK-SAME: regions = [#schedule.region<kind = active, groups = [
// CHECK-SAME: #schedule.group<fragments = [0], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [15], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [16], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [31], schedule = acyclic
// CHECK-SAME: #schedule.group<fragments = [32], schedule = acyclic
// CHECK-SAME: #schedule.region<kind = nba

module {
  simulation.design @startup_phases {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "initial0"
    simulation.code_unit.decl 3 in 0 initial hierarchy "initial1"
    simulation.code_unit.decl 4 in 0 initial hierarchy "initial2"
    simulation.code_unit.decl 5 in 0 initial hierarchy "initial3"
    simulation.code_unit.decl 6 in 0 initial hierarchy "initial4"
    simulation.code_unit.decl 7 in 0 initial hierarchy "initial5"
    simulation.code_unit.decl 8 in 0 initial hierarchy "initial6"
    simulation.code_unit.decl 9 in 0 initial hierarchy "initial7"
    simulation.code_unit.decl 10 in 0 initial hierarchy "initial8"
    simulation.code_unit.decl 11 in 0 initial hierarchy "initial9"
    simulation.code_unit.decl 12 in 0 initial hierarchy "initial10"
    simulation.code_unit.decl 13 in 0 initial hierarchy "initial11"
    simulation.code_unit.decl 14 in 0 initial hierarchy "initial12"
    simulation.code_unit.decl 15 in 0 initial hierarchy "initial13"
    simulation.code_unit.decl 16 in 0 initial hierarchy "initial14"
    simulation.code_unit.decl 17 in 0 initial hierarchy "initial15"
    simulation.code_unit.decl 18 in 0 always hierarchy "always0"
    simulation.code_unit.decl 19 in 0 always hierarchy "always1"
    simulation.code_unit.decl 20 in 0 always hierarchy "always2"
    simulation.code_unit.decl 21 in 0 always hierarchy "always3"
    simulation.code_unit.decl 22 in 0 always hierarchy "always4"
    simulation.code_unit.decl 23 in 0 always hierarchy "always5"
    simulation.code_unit.decl 24 in 0 always hierarchy "always6"
    simulation.code_unit.decl 25 in 0 always hierarchy "always7"
    simulation.code_unit.decl 26 in 0 always hierarchy "always8"
    simulation.code_unit.decl 27 in 0 always hierarchy "always9"
    simulation.code_unit.decl 28 in 0 always hierarchy "always10"
    simulation.code_unit.decl 29 in 0 always hierarchy "always11"
    simulation.code_unit.decl 30 in 0 always hierarchy "always12"
    simulation.code_unit.decl 31 in 0 always hierarchy "always13"
    simulation.code_unit.decl 32 in 0 always hierarchy "always14"
    simulation.code_unit.decl 33 in 0 always hierarchy "always15"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p_initial0 = simulation.spawn @initial0(%ctx) :
          !simulation.context -> !simulation.process
      %p_always0 = simulation.spawn @always0(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial1 = simulation.spawn @initial1(%ctx) :
          !simulation.context -> !simulation.process
      %p_always1 = simulation.spawn @always1(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial2 = simulation.spawn @initial2(%ctx) :
          !simulation.context -> !simulation.process
      %p_always2 = simulation.spawn @always2(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial3 = simulation.spawn @initial3(%ctx) :
          !simulation.context -> !simulation.process
      %p_always3 = simulation.spawn @always3(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial4 = simulation.spawn @initial4(%ctx) :
          !simulation.context -> !simulation.process
      %p_always4 = simulation.spawn @always4(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial5 = simulation.spawn @initial5(%ctx) :
          !simulation.context -> !simulation.process
      %p_always5 = simulation.spawn @always5(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial6 = simulation.spawn @initial6(%ctx) :
          !simulation.context -> !simulation.process
      %p_always6 = simulation.spawn @always6(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial7 = simulation.spawn @initial7(%ctx) :
          !simulation.context -> !simulation.process
      %p_always7 = simulation.spawn @always7(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial8 = simulation.spawn @initial8(%ctx) :
          !simulation.context -> !simulation.process
      %p_always8 = simulation.spawn @always8(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial9 = simulation.spawn @initial9(%ctx) :
          !simulation.context -> !simulation.process
      %p_always9 = simulation.spawn @always9(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial10 = simulation.spawn @initial10(%ctx) :
          !simulation.context -> !simulation.process
      %p_always10 = simulation.spawn @always10(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial11 = simulation.spawn @initial11(%ctx) :
          !simulation.context -> !simulation.process
      %p_always11 = simulation.spawn @always11(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial12 = simulation.spawn @initial12(%ctx) :
          !simulation.context -> !simulation.process
      %p_always12 = simulation.spawn @always12(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial13 = simulation.spawn @initial13(%ctx) :
          !simulation.context -> !simulation.process
      %p_always13 = simulation.spawn @always13(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial14 = simulation.spawn @initial14(%ctx) :
          !simulation.context -> !simulation.process
      %p_always14 = simulation.spawn @always14(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial15 = simulation.spawn @initial15(%ctx) :
          !simulation.context -> !simulation.process
      %p_always15 = simulation.spawn @always15(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @initial0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
    simulation.func @initial1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }
    simulation.func @initial2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
    simulation.func @initial3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      simulation.return
    }
    simulation.func @initial4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      simulation.return
    }
    simulation.func @initial5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      simulation.return
    }
    simulation.func @initial6(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      simulation.return
    }
    simulation.func @initial7(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      simulation.return
    }
    simulation.func @initial8(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 10 : i64} {
      simulation.return
    }
    simulation.func @initial9(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 11 : i64} {
      simulation.return
    }
    simulation.func @initial10(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 12 : i64} {
      simulation.return
    }
    simulation.func @initial11(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 13 : i64} {
      simulation.return
    }
    simulation.func @initial12(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 14 : i64} {
      simulation.return
    }
    simulation.func @initial13(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 15 : i64} {
      simulation.return
    }
    simulation.func @initial14(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 16 : i64} {
      simulation.return
    }
    simulation.func @initial15(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 17 : i64} {
      simulation.return
    }
    simulation.func @always0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 18 : i64} {
      simulation.return
    }
    simulation.func @always1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 19 : i64} {
      simulation.return
    }
    simulation.func @always2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 20 : i64} {
      simulation.return
    }
    simulation.func @always3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 21 : i64} {
      simulation.return
    }
    simulation.func @always4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 22 : i64} {
      simulation.return
    }
    simulation.func @always5(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 23 : i64} {
      simulation.return
    }
    simulation.func @always6(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 24 : i64} {
      simulation.return
    }
    simulation.func @always7(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 25 : i64} {
      simulation.return
    }
    simulation.func @always8(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 26 : i64} {
      simulation.return
    }
    simulation.func @always9(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 27 : i64} {
      simulation.return
    }
    simulation.func @always10(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 28 : i64} {
      simulation.return
    }
    simulation.func @always11(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 29 : i64} {
      simulation.return
    }
    simulation.func @always12(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 30 : i64} {
      simulation.return
    }
    simulation.func @always13(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 31 : i64} {
      simulation.return
    }
    simulation.func @always14(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 32 : i64} {
      simulation.return
    }
    simulation.func @always15(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 33 : i64} {
      simulation.return
    }
  }
}
