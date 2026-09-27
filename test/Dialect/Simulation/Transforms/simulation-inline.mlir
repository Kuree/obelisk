// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=0}))' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 missed-remarks=true}))' > %t.threaded 2> %t.remarks
// RUN: obelisk-opt %s --mlir-disable-threading --mlir-print-debuginfo --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 missed-remarks=true}))' > %t.serial 2> %t.serial-remarks
// RUN: diff -u %t.serial %t.threaded
// RUN: diff -u %t.serial-remarks %t.remarks
// RUN: FileCheck %s --check-prefix=O3 < %t.threaded
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks

module {
  simulation.design @inline {
    simulation.scope.decl 0 hierarchy "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.child"
    simulation.code_unit.decl 10 in 1 function hierarchy "top.child.single" debug "single" loc("single.sv":4:2)
    simulation.code_unit.decl 11 in 1 function hierarchy "top.child.multi" debug "multi"
    simulation.code_unit.decl 12 in 1 function hierarchy "top.child.void" debug "void"
    simulation.code_unit.decl 13 in 1 function hierarchy "top.child.io" debug "io"
    simulation.code_unit.decl 16 in 1 function hierarchy "top.child.unfrozen" debug "unfrozen"
    simulation.code_unit.decl 19 in 1 function hierarchy "top.child.boundary_target" debug "boundary_target"
    simulation.code_unit.decl 14 in 1 function hierarchy "top.child.unknown" debug "unknown"
    simulation.code_unit.decl 15 in 1 function hierarchy "top.child.recursive" debug "recursive"
    simulation.code_unit.decl 17 in 1 initial hierarchy "top.child.process" debug "process"
    simulation.code_unit.decl 20 in 1 function hierarchy "top.child.single_caller"
    simulation.code_unit.decl 21 in 1 function hierarchy "top.child.multi_caller"
    simulation.code_unit.decl 22 in 1 function hierarchy "top.child.void_caller"
    simulation.code_unit.decl 23 in 1 function hierarchy "top.child.io_caller"
    simulation.code_unit.decl 24 in 1 function hierarchy "top.child.blocked_caller"
    simulation.code_unit.decl 25 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.code_unit.decl 26 in 1 function hierarchy "top.child.nested_leaf"
    simulation.code_unit.decl 27 in 1 function hierarchy "top.child.nested_middle"
    simulation.code_unit.decl 28 in 1 function hierarchy "top.child.nested_caller"
    simulation.code_unit.decl 29 in 1 function hierarchy "top.child.unknown_body"
    simulation.code_unit.decl 30 in 1 function hierarchy "top.child.descriptor_target"
    simulation.code_unit.decl 31 in 1 function hierarchy "top.child.descriptor_caller"
    simulation.storage.decl 0 in 1 : !simulation.unpacked_array<0 : 3 x !simulation.logic<8>> design

    simulation.func private @single(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 10 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one {test.clone = true} : i32
          loc("callee.sv":12:3)
      simulation.return %sum : i32
    }

    simulation.func private @multi(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      cf.cond_br %condition, ^left, ^right
    ^left:
      %one = arith.constant 1 : i32
      simulation.return %one : i32
    ^right:
      %two = arith.constant 2 : i32
      simulation.return %two : i32
    }

    simulation.func private @void(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 8 : i32} {
      simulation.return
    }

    simulation.func private @io(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %fd: i32 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 13 : i64, entry_kind = 8 : i32} {
      %format = simulation.bytes.constant "%m"
      simulation.display %ctx to %fd(%format) newline = true radix = <decimal>
          flags = [0] {scope = "top.child.io"} : !simulation.bytes
      simulation.return
    }

    simulation.func private @unknown(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 14 : i64, entry_kind = 8 : i32,
                    simulation.future_semantics = true} {
      simulation.return %value : i32
    }

    simulation.func private @unfrozen(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %fd: i32 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 16 : i64, entry_kind = 8 : i32} {
      %format = simulation.bytes.constant "%m"
      simulation.display %ctx to %fd(%format) newline = true radix = <decimal>
          flags = [0] : !simulation.bytes
      simulation.return
    }

    simulation.func private @boundary_target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 19 : i64, entry_kind = 8 : i32} {
      simulation.return %value : i32
    }

    simulation.func private @unknown_body(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 29 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one {simulation.future_body = true} : i32
      simulation.return %result : i32
    }

    simulation.func private @nested_leaf(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 26 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one : i32
      simulation.return %result : i32
    }

    // Exercise every real view-descriptor field accepted on an inline
    // boundary: aggregate element 2 followed by packed bits [5:2].
    simulation.func private @descriptor_target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %slice: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !simulation.unpacked_array<0 : 3 x !simulation.logic<8>>, simulation.descriptor_low = 18 : i64, simulation.descriptor_indices = array<i64: 2>, simulation.descriptor_aggregate_type = !simulation.logic<8>, simulation.descriptor_packed_low = 2 : i64})
        -> !simulation.logic<4>
        attributes {code_unit_id = 30 : i64, entry_kind = 8 : i32} {
      %value = simulation.ref.load %slice : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.return %value : !simulation.logic<4>
    }

    simulation.func private @nested_middle(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 27 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @nested_leaf(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    simulation.func private @recursive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 15 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %done = arith.cmpi eq, %value, %zero : i32
      cf.cond_br %done, ^base, ^step
    ^base:
      simulation.return %zero : i32
    ^step:
      %one = arith.constant 1 : i32
      %next = arith.subi %value, %one : i32
      %result = simulation.call @recursive(%ctx, %next)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    simulation.func private @external(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    simulation.func @single_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 20 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @single(%ctx, %value)
          : (!simulation.context, i32) -> i32 loc("caller.sv":20:5)
      simulation.return %result : i32
    }

    simulation.func @multi_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 21 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @multi(%ctx, %condition)
          : (!simulation.context, i1) -> i32
      simulation.return %result : i32
    }

    simulation.func @void_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 22 : i64, entry_kind = 8 : i32} {
      simulation.call @void(%ctx) : (!simulation.context) -> ()
      simulation.return
    }

    simulation.func @io_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %fd: i32 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 23 : i64, entry_kind = 8 : i32} {
      simulation.call @io(%ctx, %fd) : (!simulation.context, i32) -> ()
      simulation.return
    }

    simulation.func @blocked_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 24 : i64, entry_kind = 8 : i32} {
      %unknown = simulation.call @unknown(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.call @unfrozen(%ctx, %unknown)
          : (!simulation.context, i32) -> ()
      %external = simulation.call @external(%ctx, %unknown)
          : (!simulation.context, i32) -> i32
      %recursive = simulation.call @recursive(%ctx, %external)
          : (!simulation.context, i32) -> i32
      %boundary = simulation.call @boundary_target(%ctx, %recursive)
          {arg_attrs = [{}, {simulation.future_boundary = true}]}
          : (!simulation.context, i32) -> i32
      %body = simulation.call @unknown_body(%ctx, %boundary)
          : (!simulation.context, i32) -> i32
      simulation.return %recursive : i32
    }

    simulation.func @nested_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 28 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @nested_middle(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    simulation.func @descriptor_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %slice: !simulation.ref<!simulation.logic<4>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !simulation.unpacked_array<0 : 3 x !simulation.logic<8>>, simulation.descriptor_low = 18 : i64, simulation.descriptor_indices = array<i64: 2>, simulation.descriptor_aggregate_type = !simulation.logic<8>, simulation.descriptor_packed_low = 2 : i64})
        -> !simulation.logic<4>
        attributes {code_unit_id = 31 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @descriptor_target(%ctx, %slice)
          : (!simulation.context, !simulation.ref<!simulation.logic<4>>) -> !simulation.logic<4>
      simulation.return %result : !simulation.logic<4>
    }

    simulation.func private @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 1 : i32} {
      simulation.return
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 25 : i64, entry_kind = 0 : i32} {
      %handle = simulation.spawn @process(%ctx)
          : !simulation.context -> !simulation.process
      simulation.return
    }
  }
}

// O0: simulation.func private @single
// O0: simulation.call @single
// O0: simulation.call @multi
// O0: simulation.call @void
// O0: simulation.call @io
// O0: simulation.call @descriptor_target

// The executable function can disappear while its immutable inventory record
// remains available to reflection.
// O3: simulation.code_unit.decl 10 in 1 function hierarchy "top.child.single" debug "single"
// O3-SAME: loc(
// O3-NOT: simulation.func private @single
// O3-NOT: simulation.func private @multi
// O3-NOT: simulation.func private @void
// O3-NOT: simulation.func private @io
// O3-NOT: simulation.func private @descriptor_target

// O3-LABEL: simulation.func private @recursive
// O3: simulation.call @recursive

// O3-LABEL: simulation.func @single_caller
// O3: arith.addi {{.*}} {test.clone = true}
// O3-SAME: loc(#loc[[CALLSITE:[0-9]+]])
// O3-NOT: simulation.call @single

// O3-LABEL: simulation.func @multi_caller
// O3: cf.cond_br
// O3: cf.br
// O3-NOT: simulation.call @multi

// O3-LABEL: simulation.func @void_caller
// O3-NOT: simulation.call @void

// O3-LABEL: simulation.func @io_caller
// O3: simulation.display
// O3-SAME: {scope = "top.child.io"}
// O3-NOT: simulation.call @io

// O3-LABEL: simulation.func @blocked_caller
// O3: simulation.call @unknown
// O3: simulation.call @unfrozen
// O3: simulation.call @external
// O3: simulation.call @recursive
// O3: simulation.call @boundary_target
// O3: simulation.call @unknown_body
// O3-LABEL: simulation.func @nested_caller
// O3-NOT: simulation.call
// O3-LABEL: simulation.func @descriptor_caller
// O3: simulation.ref.load
// O3-NOT: simulation.call
// O3-LABEL: simulation.func @root
// O3: simulation.spawn @process
// O3: #loc[[CALLSITE]] = loc(callsite(#loc{{[0-9]+}} at #loc{{[0-9]+}}))

// REMARK-DAG: not inlined: callee contains unknown simulation metadata
// REMARK-DAG: not inlined: display has no frozen lexical scope
// REMARK-DAG: not inlined: callee is not a defined zero-time function
// REMARK-DAG: not inlined: call is in a recursive SCC
// REMARK-DAG: not inlined: call boundary contains unknown simulation metadata
