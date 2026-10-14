// RUN: obelisk-opt %s --obelisk-sim-insert-gc-polls | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-insert-gc-polls \
// RUN:   --obelisk-sim-insert-gc-polls | FileCheck %s

// Under polled collection allocation never collects, so garbage is bounded
// only if every way to allocate repeatedly without returning to the scheduler
// passes a poll. A UVM-shaped class hierarchy exercises each case: allocating
// loops (directly and through calls), loops a delay cuts and a task call does
// not, recursion, closed-world virtual dispatch, a DPI export foreign code may
// call in a loop, and the context imports that may reach it. Calls that cannot reach a poll are marked poll-free, so root
// instrumentation can skip them. Rerunning the pass adds nothing.

// CHECK: module attributes {gc.polled}
module {
  simulation.design @polls {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 2 in 0 function hierarchy "Node.weight"
    simulation.code_unit.decl 3 in 0 function hierarchy "Heavy.weight"
    simulation.code_unit.decl 4 in 0 function hierarchy "make"
    simulation.code_unit.decl 5 in 0 function hierarchy "count"
    simulation.code_unit.decl 6 in 0 function hierarchy "build"
    simulation.code_unit.decl 7 in 0 function hierarchy "chain"
    simulation.code_unit.decl 8 in 0 function hierarchy "exported"
    simulation.code_unit.decl 9 in 0 function hierarchy "Light.weight"
    simulation.code_unit.decl 10 in 0 task hierarchy "fill"
    simulation.code_unit.decl 11 in 0 initial hierarchy "tasks"
    simulation.code_unit.decl 12 in 0 initial hierarchy "boxes"
    simulation.code_unit.decl 13 in 0 function hierarchy "opaque"
    simulation.code_unit.decl 14 in 0 function hierarchy "early"
    simulation.code_unit.decl 15 in 0 function hierarchy "late"

    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Heavy id 2 extends @Node {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.decl @Light id 3 extends @Node {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.method @Node_weight of @Node slot 0 signature_id 11
        implemented_by @node_weight :
      (!simulation.context, !simulation.class_handle<@Node>) -> i32 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Heavy_weight of @Heavy slot 0 signature_id 11
        implemented_by @heavy_weight :
      (!simulation.context, !simulation.class_handle<@Heavy>) -> i32 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @Light_weight of @Light slot 0 signature_id 11
        implemented_by @light_weight :
      (!simulation.context, !simulation.class_handle<@Light>) -> i32 {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }

    // CHECK-LABEL: simulation.func private @node_weight(
    // CHECK-NOT: simulation.gc.safepoint
    // CHECK: simulation.return
    simulation.func private @node_weight(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Node>
          {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      simulation.return %one : i32
    }
    simulation.func private @light_weight(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Light>
          {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9 : i64, entry_kind = 8 : i32} {
      %two = arith.constant 2 : i32
      simulation.return %two : i32
    }

    // An allocation on a loop polls once per iteration, before the first
    // allocation of the loop body.
    // CHECK-LABEL: simulation.func private @heavy_weight(
    // CHECK: ^bb2:
    // CHECK-NEXT: simulation.gc.safepoint %{{.*}} : !simulation.context
    // CHECK-NEXT: simulation.class.alloc
    // CHECK-NEXT: simulation.class.alloc
    // CHECK-NOT: simulation.gc.safepoint
    // CHECK: simulation.return
    simulation.func private @heavy_weight(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Heavy>
          {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %eight = arith.constant 8 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %eight : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %scratch = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      %other = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^exit:
      simulation.return %i : i32
    }

    // One allocation outside any cycle needs no poll.
    // CHECK-LABEL: simulation.func private @make(
    // CHECK-NOT: simulation.gc.safepoint
    // CHECK: simulation.return
    simulation.func private @make(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !simulation.class_handle<@Node>
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %node = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      simulation.return %node : !simulation.class_handle<@Node>
    }

    // A loop without allocation stays free of polls.
    // CHECK-LABEL: simulation.func private @count(
    // CHECK-NOT: simulation.gc.safepoint
    // CHECK: simulation.return
    simulation.func private @count(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %n: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %n : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^exit:
      simulation.return %i : i32
    }

    // A loop allocates through a call to an allocating, poll-free function:
    // the poll precedes the call, which stays poll-free itself.
    // CHECK-LABEL: simulation.func private @build(
    // CHECK: simulation.call @count({{.*}}) {simulation.gc_poll_free}
    // CHECK: ^bb2:
    // CHECK-NEXT: simulation.gc.safepoint
    // CHECK-NEXT: simulation.call @make({{.*}}) {simulation.gc_poll_free}
    simulation.func private @build(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %n: i32 {simulation.capture_kind = 2 : i32})
        attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = simulation.call @count(%ctx, %n) :
        (!simulation.context, i32) -> i32
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %node = simulation.call @make(%ctx) :
        (!simulation.context) -> !simulation.class_handle<@Node>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^exit:
      simulation.return
    }

    // Recursion allocates without a loop: each recursive call polls first,
    // and may reach that poll.
    // CHECK-LABEL: simulation.func private @chain(
    // CHECK: simulation.class.alloc
    // CHECK-NOT: simulation.gc.safepoint
    // CHECK: ^bb1:
    // CHECK: simulation.gc.safepoint
    // CHECK-NEXT: simulation.call @chain({{.*}}) :
    simulation.func private @chain(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %depth: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {code_unit_id = 7 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %node = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      %done = arith.cmpi eq, %depth, %zero : i32
      cf.cond_br %done, ^exit, ^step
    ^step:
      %less = arith.subi %depth, %one : i32
      %inner = simulation.call @chain(%ctx, %less) :
        (!simulation.context, i32) -> i32
      simulation.return %inner : i32
    ^exit:
      simulation.return %zero : i32
    }

    // Foreign code may call an export in a loop of its own.
    // CHECK-LABEL: simulation.func @exported(
    // CHECK-NEXT: simulation.gc.safepoint
    // CHECK-NEXT: simulation.class.alloc
    simulation.func @exported(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {
          code_unit_id = 8 : i64, entry_kind = 8 : i32,
          dpi.export
        } {
      %node = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      simulation.return
    }

    // A task call may run the task in the caller's logical process without a
    // scheduler pass, so it does not cut a cycle: a loop of calls to an
    // allocating task polls before each call.
    // CHECK-LABEL: simulation.func @tasks(
    // CHECK: ^bb1:
    // CHECK-NEXT: simulation.gc.safepoint
    // CHECK-NEXT: simulation.task.call @fill
    simulation.func private @fill(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 10 : i64, entry_kind = 12 : i32} {
      %node = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      simulation.return
    }
    simulation.func @tasks(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 11 : i64, entry_kind = 1 : i32} {
      cf.br ^loop
    ^loop:
      simulation.task.call @fill(%ctx) arguments 1 to ^loop :
        !simulation.context
    }

    // Every operation with the AllocatesManagedStorage trait counts, such as
    // mailbox construction. A callee without a body is unresolved: it may
    // allocate and re-enter its caller, so each call polls first and may
    // reach a poll.
    // CHECK-LABEL: simulation.func @boxes(
    // CHECK: simulation.gc.safepoint
    // CHECK-NEXT: simulation.call @opaque(
    // CHECK-NOT: gc_poll_free
    // CHECK-SAME: -> ()
    // CHECK: ^bb1:
    // CHECK-NEXT: simulation.gc.safepoint
    // CHECK-NEXT: simulation.mailbox.create
    simulation.func private @opaque(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 13 : i64, entry_kind = 8 : i32}
    simulation.func @boxes(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %bound: i64 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 1 : i32} {
      simulation.call @opaque(%ctx) : (!simulation.context) -> ()
      cf.br ^loop
    ^loop:
      %mailbox = simulation.mailbox.create %bound {
        alignment = 8 : i64,
        bit_width = 0 : i64,
        element_flags = #simulation.element_flags<none>,
        element_kind = #simulation.element_kind<string>,
        trace_kinds = array<i32>,
        trace_offsets = array<i64>,
        type_id = 1 : i64,
        value_size = 8 : i64
      } :
        (i64) -> !simulation.mailbox<!simulation.string>
      cf.br ^loop
    }

    // The call-graph closure does not depend on definition order: a loop in a
    // function defined before its allocating callee still polls.
    // CHECK-LABEL: simulation.func private @early(
    // CHECK: ^bb1:
    // CHECK-NEXT: simulation.gc.safepoint
    // CHECK-NEXT: simulation.call @late({{.*}}) {simulation.gc_poll_free}
    simulation.func private @early(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 14 : i64, entry_kind = 8 : i32} {
      cf.br ^loop
    ^loop:
      simulation.call @late(%ctx) : (!simulation.context) -> ()
      cf.br ^loop
    }
    simulation.func private @late(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 15 : i64, entry_kind = 8 : i32} {
      %node = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      simulation.return
    }

    // CHECK-LABEL: simulation.func @driver(
    simulation.func @driver(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %ten = arith.constant 10 : i32
      %node = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      // CHECK: simulation.call @make({{.*}}) {simulation.gc_poll_free}
      %made = simulation.call @make(%ctx) :
        (!simulation.context) -> !simulation.class_handle<@Node>
      // CHECK: simulation.call @build(
      // CHECK-NOT: gc_poll_free
      // CHECK-SAME: -> ()
      simulation.call @build(%ctx, %ten) : (!simulation.context, i32) -> ()
      // CHECK: simulation.call @chain(
      // CHECK-NOT: gc_poll_free
      // CHECK-SAME: -> i32
      %deep = simulation.call @chain(%ctx, %ten) :
        (!simulation.context, i32) -> i32
      // A @Node receiver may be a @Heavy, whose override polls.
      // CHECK: simulation.class.virtual_call %{{.*}}[@Node_weight]
      // CHECK-NOT: gc_poll_free
      // CHECK-SAME: -> i32
      %weight = simulation.class.virtual_call
        %node[@Node_weight] slot 0 signature_id 11() :
        (!simulation.class_handle<@Node>) -> i32
      // A @Light receiver selects only its poll-free override.
      // CHECK: simulation.class.virtual_call %{{.*}}[@Light_weight]
      // CHECK-SAME: {simulation.gc_poll_free}
      %light = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Light>
      %light_weight = simulation.class.virtual_call
        %light[@Light_weight] slot 0 signature_id 11() :
        (!simulation.class_handle<@Light>) -> i32
      // A context import may call the export, which polls; a noncontext one
      // cannot (IEEE 1800-2023 35.5.3).
      // CHECK: simulation.dpi.call "host"{{.*}}is_task = false, source_column
      %host = simulation.dpi.call "host" id 200 scope 0
          context %ctx : !simulation.context() {
            abi_signature = [], is_context = true, is_pure = false,
            is_task = false, source_column = 1 : i32,
            source_file = "polls.sv", source_line = 1 : i32
          } : () -> !runtime.status
      // CHECK: simulation.dpi.call "leaf"{{.*}}is_task = false, simulation.gc_poll_free, source_column
      %leaf = simulation.dpi.call "leaf" id 201 scope 0
          context %ctx : !simulation.context() {
            abi_signature = [], is_context = false, is_pure = false,
            is_task = false, source_column = 1 : i32,
            source_file = "polls.sv", source_line = 2 : i32
          } : () -> !runtime.status
      cf.br ^tick
    // A cycle through a delay returns to the scheduler every iteration, which
    // reclaims between activations: no poll.
    // CHECK: simulation.suspend.delay
    // CHECK-NOT: simulation.gc.safepoint
    // CHECK: simulation.class.alloc
    // CHECK-NEXT: cf.br
    ^tick:
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^tock
    ^tock:
      %fresh = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@Node>
      cf.br ^tick
    }
  }
}
