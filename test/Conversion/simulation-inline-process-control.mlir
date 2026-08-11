// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=0}))' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3}))' | FileCheck %s --check-prefix=O3

module {
  obelisk_sim.design @inline_process_control {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.class.decl @Box id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.control"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.outer"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.safe"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "top.actor"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "top.class_control"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "top.class_safe"

    obelisk_sim.func private @control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.process.control suspend %process to ^continued(%value : i32)
    ^continued(%forwarded: i32):
      obelisk_sim.return %forwarded : i32
    }

    obelisk_sim.func private @outer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %controlled = obelisk_sim.call @control(%ctx, %process, %value) :
          (!obelisk_sim.context, !obelisk_sim.process, i32) -> i32
      %one = arith.constant 1 : i32
      %result = arith.addi %controlled, %one : i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @safe(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one : i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @class_control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Box> {obelisk_sim.capture_kind = 1 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      obelisk_sim.process.control resume %process to ^continued(%value : i32)
    ^continued(%forwarded: i32):
      obelisk_sim.return %forwarded : i32
    }

    obelisk_sim.func private @class_safe(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Box> {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one : i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func @actor(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %current = obelisk_sim.process.current
      %seven = arith.constant 7 : i32
      %box = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@Box>
      %method = obelisk_sim.class.direct_call @class_control
          %box(%current, %seven) :
          (!obelisk_sim.class_handle<@Box>, !obelisk_sim.process, i32) -> i32
      %controlled = obelisk_sim.call @outer(%ctx, %current, %method) :
          (!obelisk_sim.context, !obelisk_sim.process, i32) -> i32
      %safe = obelisk_sim.call @safe(%ctx, %controlled) :
          (!obelisk_sim.context, i32) -> i32
      %method_safe = obelisk_sim.class.direct_call @class_safe %box(%safe) :
          (!obelisk_sim.class_handle<@Box>, i32) -> i32
      %storage = obelisk_sim.ref.alloc %method_safe : i32 -> !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
  }
}

// Mandatory process-control propagation runs even at O0, including through
// the transitive @outer boundary. The unrelated safe call remains outlined.
// O0-LABEL: obelisk_sim.func @actor(
// O0-NOT: obelisk_sim.class.direct_call @class_control
// O0-NOT: obelisk_sim.call @class_control
// O0-NOT: obelisk_sim.call @outer
// O0-NOT: obelisk_sim.call @control
// O0: obelisk_sim.process.control resume %{{.*}} to ^[[METHOD_CONT:[a-zA-Z0-9_]+]]
// O0: ^[[METHOD_CONT]]
// O0: obelisk_sim.process.control suspend %{{.*}} to ^[[CONT:[a-zA-Z0-9_]+]]
// O0: ^[[CONT]]
// O0: %[[SAFE:.*]] = obelisk_sim.call @safe
// O0: %[[METHOD_SAFE:.*]] = obelisk_sim.class.direct_call @class_safe %{{.*}}(%[[SAFE]])
// O0: obelisk_sim.ref.alloc %[[METHOD_SAFE]]
// O0: obelisk_sim.return

// At O3 the ordinary tiny, safe zero-time function remains eligible for the
// profitability-driven inliner after mandatory propagation.
// O3-LABEL: obelisk_sim.func @actor(
// O3-NOT: obelisk_sim.class.direct_call
// O3-NOT: obelisk_sim.call
// O3: obelisk_sim.process.control resume %{{.*}} to ^[[METHOD_CONT:[a-zA-Z0-9_]+]]
// O3: ^[[METHOD_CONT]]
// O3: obelisk_sim.process.control suspend %{{.*}} to ^[[CONT:[a-zA-Z0-9_]+]]
// O3: ^[[CONT]]
// O3: arith.addi
// O3: arith.addi
// O3: arith.addi
// O3: obelisk_sim.ref.alloc
// O3: obelisk_sim.return
