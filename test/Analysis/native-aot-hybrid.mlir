// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// CHECK: native-aot eligible=true fully=false
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @native
// CHECK-NEXT: actor 2 @managed
// CHECK-NEXT: bytecode @clock_coordinator bb0
// CHECK-NEXT: bytecode @real_reactive bb0
// CHECK-NEXT: reason clock cohort wait requires runtime ordering
// CHECK-NEXT: reason real-valued reactive state requires bytecode

module {
  simulation.design @hybrid {
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "native"
    simulation.code_unit.decl 3 in 0 initial hierarchy "managed"
    simulation.code_unit.decl 4 in 0 initial hierarchy "real_reactive"
    simulation.code_unit.decl 5 in 0 always hierarchy "clock_coordinator"
    simulation.scope.decl 0

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %native = simulation.spawn @native(%ctx) :
          !simulation.context -> !simulation.process
      %managed = simulation.spawn @managed(%ctx) :
          !simulation.context -> !simulation.process
      %real = simulation.spawn @real_reactive(%ctx) :
          !simulation.context -> !simulation.process
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %clock = simulation.ref.alloc %zero : !simulation.logic<1> ->
          !simulation.ref<!simulation.logic<1>>
      %coordinator = simulation.spawn @clock_coordinator(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>> ->
          !simulation.process
      simulation.return
    }

    simulation.func @native(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.return
    }

    simulation.func @managed(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %text = simulation.string.literal "native"
      simulation.return
    }

    simulation.func @real_reactive(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0.0 : f64
      simulation.return
    }

    simulation.func private @clock_coordinator(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    simulation.multiclock_sequence_coordinator} {
      simulation.suspend.clock_set %clock conditions 0 edges [1]
          indices [-1] site 17 to ^done :
          !simulation.ref<!simulation.logic<1>>
    ^done:
      simulation.return
    }
  }
}
