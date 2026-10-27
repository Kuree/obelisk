// RUN: talos-opt %s -o /dev/null --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph),test-talos-simulation-schedule-analysis)' 2>&1 | FileCheck %s

// Keep distinct root blocks and event regions separate. An always process
// marked starts_without_waiting does not precede initial0; always1 is spawned
// in another block and orders only initial1. Unspawned always2 adds no phase.
// These are the ranks produced by the original explicit-edge representation.
// CHECK: schedule @startup_phases
// CHECK-NEXT: func @a_initial0 entry=0
// CHECK-NEXT: bb0 rank=0
// CHECK-NEXT: func @a_initial1 entry=5
// CHECK-NEXT: bb0 rank=5
// CHECK-NEXT: func @a_initial2 entry=7
// CHECK-NEXT: bb0 rank=7
// CHECK-NEXT: func @root entry=1
// CHECK-NEXT: bb0 rank=1
// CHECK-NEXT: bb1 rank=2
// CHECK-NEXT: func @z_always0 entry=3
// CHECK-NEXT: bb0 rank=3
// CHECK-NEXT: func @z_always1 entry=4
// CHECK-NEXT: bb0 rank=4
// CHECK-NEXT: func @z_always2 entry=6
// CHECK-NEXT: bb0 rank=6

module {
  simulation.design @startup_phases {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "initial0"
    simulation.code_unit.decl 3 in 0 initial hierarchy "initial1"
    simulation.code_unit.decl 4 in 0 initial hierarchy "initial2"
    simulation.code_unit.decl 5 in 0 always hierarchy "always0"
    simulation.code_unit.decl 6 in 0 always hierarchy "always1"
    simulation.code_unit.decl 7 in 0 always hierarchy "always2"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %p_initial0 = simulation.spawn @a_initial0(%ctx) :
          !simulation.context -> !simulation.process
      %p_always0 = simulation.spawn @z_always0(%ctx) :
          !simulation.context -> !simulation.process
      cf.br ^second
    ^second:
      %p_initial1 = simulation.spawn @a_initial1(%ctx) :
          !simulation.context -> !simulation.process
      %p_always1 = simulation.spawn @z_always1(%ctx) :
          !simulation.context -> !simulation.process
      %p_initial2 = simulation.spawn @a_initial2(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @a_initial0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }
    simulation.func @a_initial1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }
    simulation.func @a_initial2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64, home_region = 10 : i32} {
      simulation.return
    }
    simulation.func @z_always0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64, schedule.starts_without_waiting} {
      simulation.return
    }
    simulation.func @z_always1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      simulation.return
    }
    simulation.func @z_always2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      simulation.return
    }
  }
}
