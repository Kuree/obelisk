// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=0}))' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3}))' | FileCheck %s --check-prefix=O3

module {
  simulation.design @inline_process_control {
    simulation.scope.decl 0 hierarchy "top"
    simulation.class.decl @Box id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.code_unit.decl 1 in 0 function hierarchy "top.control"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.outer"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.safe"
    simulation.code_unit.decl 4 in 0 initial hierarchy "top.actor"
    simulation.code_unit.decl 5 in 0 function hierarchy "top.class_control"
    simulation.code_unit.decl 6 in 0 function hierarchy "top.class_safe"

    simulation.func private @control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      simulation.process.control suspend %process to ^continued(%value : i32)
    ^continued(%forwarded: i32):
      simulation.return %forwarded : i32
    }

    simulation.func private @outer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %controlled = simulation.call @control(%ctx, %process, %value) :
          (!simulation.context, !simulation.process, i32) -> i32
      %one = arith.constant 1 : i32
      %result = arith.addi %controlled, %one : i32
      simulation.return %result : i32
    }

    simulation.func private @safe(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one : i32
      simulation.return %result : i32
    }

    simulation.func private @class_control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Box> {simulation.capture_kind = 1 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      simulation.process.control resume %process to ^continued(%value : i32)
    ^continued(%forwarded: i32):
      simulation.return %forwarded : i32
    }

    simulation.func private @class_safe(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Box> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one : i32
      simulation.return %result : i32
    }

    simulation.func @actor(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %current = simulation.process.current
      %seven = arith.constant 7 : i32
      %box = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Box>
      %method = simulation.class.direct_call @class_control
          %box(%current, %seven) :
          (!simulation.class_handle<@Box>, !simulation.process, i32) -> i32
      %controlled = simulation.call @outer(%ctx, %current, %method) :
          (!simulation.context, !simulation.process, i32) -> i32
      %safe = simulation.call @safe(%ctx, %controlled) :
          (!simulation.context, i32) -> i32
      %method_safe = simulation.class.direct_call @class_safe %box(%safe) :
          (!simulation.class_handle<@Box>, i32) -> i32
      %storage = simulation.ref.alloc %method_safe : i32 -> !simulation.ref<i32>
      simulation.return
    }
  }
}

// Mandatory suspend propagation runs even at O0, including through the
// transitive @outer boundary. Resume is safe at a bytecode callable boundary,
// and the unrelated safe call remains outlined.
// O0-LABEL: simulation.func @actor(
// O0-NOT: simulation.call @class_control
// O0-NOT: simulation.call @outer
// O0-NOT: simulation.call @control
// O0: %[[METHOD:.*]] = simulation.class.direct_call @class_control
// O0: simulation.process.control suspend %{{.*}} to ^[[CONT:[a-zA-Z0-9_]+]]
// O0: ^[[CONT]]
// O0: %[[SAFE:.*]] = simulation.call @safe
// O0: %[[METHOD_SAFE:.*]] = simulation.class.direct_call @class_safe %{{.*}}(%[[SAFE]])
// O0: simulation.ref.alloc %[[METHOD_SAFE]]
// O0: simulation.return

// At O3 the ordinary tiny, safe zero-time function remains eligible for the
// profitability-driven inliner after mandatory propagation.
// O3-LABEL: simulation.func @actor(
// O3-NOT: simulation.class.direct_call
// O3-NOT: simulation.call
// O3: simulation.process.control resume %{{.*}} to ^[[METHOD_CONT:[a-zA-Z0-9_]+]]
// O3: ^[[METHOD_CONT]]
// O3: simulation.process.control suspend %{{.*}} to ^[[CONT:[a-zA-Z0-9_]+]]
// O3: ^[[CONT]]
// O3: arith.addi
// O3: arith.addi
// O3: arith.addi
// O3: simulation.ref.alloc
// O3: simulation.return
