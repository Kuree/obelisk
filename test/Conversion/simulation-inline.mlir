// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=0}))' | FileCheck %s --check-prefix=O0
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 missed-remarks=true}))' > %t.threaded 2> %t.remarks
// RUN: obelisk-opt %s --mlir-disable-threading --mlir-print-debuginfo --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 missed-remarks=true}))' > %t.serial 2> %t.serial-remarks
// RUN: diff -u %t.serial %t.threaded
// RUN: diff -u %t.serial-remarks %t.remarks
// RUN: FileCheck %s --check-prefix=O3 < %t.threaded
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks

module {
  obelisk_sim.design @inline {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.child"
    obelisk_sim.code_unit.decl 10 in 1 function hierarchy "top.child.single" debug "single" loc("single.sv":4:2)
    obelisk_sim.code_unit.decl 11 in 1 function hierarchy "top.child.multi" debug "multi"
    obelisk_sim.code_unit.decl 12 in 1 function hierarchy "top.child.void" debug "void"
    obelisk_sim.code_unit.decl 13 in 1 function hierarchy "top.child.io" debug "io"
    obelisk_sim.code_unit.decl 16 in 1 function hierarchy "top.child.unfrozen" debug "unfrozen"
    obelisk_sim.code_unit.decl 19 in 1 function hierarchy "top.child.boundary_target" debug "boundary_target"
    obelisk_sim.code_unit.decl 14 in 1 function hierarchy "top.child.unknown" debug "unknown"
    obelisk_sim.code_unit.decl 15 in 1 function hierarchy "top.child.recursive" debug "recursive"
    obelisk_sim.code_unit.decl 17 in 1 initial hierarchy "top.child.process" debug "process"
    obelisk_sim.code_unit.decl 20 in 1 function hierarchy "top.child.single_caller"
    obelisk_sim.code_unit.decl 21 in 1 function hierarchy "top.child.multi_caller"
    obelisk_sim.code_unit.decl 22 in 1 function hierarchy "top.child.void_caller"
    obelisk_sim.code_unit.decl 23 in 1 function hierarchy "top.child.io_caller"
    obelisk_sim.code_unit.decl 24 in 1 function hierarchy "top.child.blocked_caller"
    obelisk_sim.code_unit.decl 25 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.code_unit.decl 26 in 1 function hierarchy "top.child.nested_leaf"
    obelisk_sim.code_unit.decl 27 in 1 function hierarchy "top.child.nested_middle"
    obelisk_sim.code_unit.decl 28 in 1 function hierarchy "top.child.nested_caller"
    obelisk_sim.code_unit.decl 29 in 1 function hierarchy "top.child.unknown_body"

    obelisk_sim.func private @single(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 10 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one {test.clone = true} : i32
          loc("callee.sv":12:3)
      obelisk_sim.return %sum : i32
    }

    obelisk_sim.func private @multi(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32} {
      cf.cond_br %condition, ^left, ^right
    ^left:
      %one = arith.constant 1 : i32
      obelisk_sim.return %one : i32
    ^right:
      %two = arith.constant 2 : i32
      obelisk_sim.return %two : i32
    }

    obelisk_sim.func private @void(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 12 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func private @io(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %fd: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 13 : i64, entry_kind = 8 : i32} {
      %format = obelisk_sim.bytes.constant "%m"
      obelisk_sim.display %ctx to %fd(%format) newline = true radix = 10
          flags = [0] {scope = "top.child.io"} : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @unknown(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 14 : i64, entry_kind = 8 : i32,
                    obelisk_sim.future_semantics = true} {
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @unfrozen(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %fd: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 16 : i64, entry_kind = 8 : i32} {
      %format = obelisk_sim.bytes.constant "%m"
      obelisk_sim.display %ctx to %fd(%format) newline = true radix = 10
          flags = [0] : !obelisk_sim.bytes
      obelisk_sim.return
    }

    obelisk_sim.func private @boundary_target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 19 : i64, entry_kind = 8 : i32} {
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @unknown_body(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 29 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one {obelisk_sim.future_body = true} : i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @nested_leaf(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 26 : i64, entry_kind = 8 : i32} {
      %one = arith.constant 1 : i32
      %result = arith.addi %value, %one : i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @nested_middle(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 27 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @nested_leaf(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @recursive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 15 : i64, entry_kind = 8 : i32} {
      %zero = arith.constant 0 : i32
      %done = arith.cmpi eq, %value, %zero : i32
      cf.cond_br %done, ^base, ^step
    ^base:
      obelisk_sim.return %zero : i32
    ^step:
      %one = arith.constant 1 : i32
      %next = arith.subi %value, %one : i32
      %result = obelisk_sim.call @recursive(%ctx, %next)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @external(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32}

    obelisk_sim.func @single_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 20 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @single(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32 loc("caller.sv":20:5)
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func @multi_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 21 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @multi(%ctx, %condition)
          : (!obelisk_sim.context, i1) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func @void_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 22 : i64, entry_kind = 8 : i32} {
      obelisk_sim.call @void(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @io_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %fd: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 23 : i64, entry_kind = 8 : i32} {
      obelisk_sim.call @io(%ctx, %fd) : (!obelisk_sim.context, i32) -> ()
      obelisk_sim.return
    }

    obelisk_sim.func @blocked_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 24 : i64, entry_kind = 8 : i32} {
      %unknown = obelisk_sim.call @unknown(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.call @unfrozen(%ctx, %unknown)
          : (!obelisk_sim.context, i32) -> ()
      %external = obelisk_sim.call @external(%ctx, %unknown)
          : (!obelisk_sim.context, i32) -> i32
      %recursive = obelisk_sim.call @recursive(%ctx, %external)
          : (!obelisk_sim.context, i32) -> i32
      %boundary = obelisk_sim.call @boundary_target(%ctx, %recursive)
          {arg_attrs = [{}, {obelisk_sim.future_boundary = true}]}
          : (!obelisk_sim.context, i32) -> i32
      %body = obelisk_sim.call @unknown_body(%ctx, %boundary)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %recursive : i32
    }

    obelisk_sim.func @nested_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 28 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @nested_middle(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 17 : i64, entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 25 : i64, entry_kind = 0 : i32} {
      %handle = obelisk_sim.spawn @process(%ctx)
          : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
  }
}

// O0: obelisk_sim.func private @single
// O0: obelisk_sim.call @single
// O0: obelisk_sim.call @multi
// O0: obelisk_sim.call @void
// O0: obelisk_sim.call @io

// The executable function can disappear while its immutable inventory record
// remains available to reflection.
// O3: obelisk_sim.code_unit.decl 10 in 1 function hierarchy "top.child.single" debug "single"
// O3-SAME: loc(
// O3-NOT: obelisk_sim.func private @single
// O3-NOT: obelisk_sim.func private @multi
// O3-NOT: obelisk_sim.func private @void
// O3-NOT: obelisk_sim.func private @io

// O3-LABEL: obelisk_sim.func private @recursive
// O3: obelisk_sim.call @recursive

// O3-LABEL: obelisk_sim.func @single_caller
// O3: arith.addi {{.*}} {test.clone = true}
// O3-SAME: loc(#loc[[CALLSITE:[0-9]+]])
// O3-NOT: obelisk_sim.call @single

// O3-LABEL: obelisk_sim.func @multi_caller
// O3: cf.cond_br
// O3: cf.br
// O3-NOT: obelisk_sim.call @multi

// O3-LABEL: obelisk_sim.func @void_caller
// O3-NOT: obelisk_sim.call @void

// O3-LABEL: obelisk_sim.func @io_caller
// O3: obelisk_sim.display
// O3-SAME: {scope = "top.child.io"}
// O3-NOT: obelisk_sim.call @io

// O3-LABEL: obelisk_sim.func @blocked_caller
// O3: obelisk_sim.call @unknown
// O3: obelisk_sim.call @unfrozen
// O3: obelisk_sim.call @external
// O3: obelisk_sim.call @recursive
// O3: obelisk_sim.call @boundary_target
// O3: obelisk_sim.call @unknown_body
// O3-LABEL: obelisk_sim.func @nested_caller
// O3-NOT: obelisk_sim.call
// O3-LABEL: obelisk_sim.func @root
// O3: obelisk_sim.spawn @process
// O3: #loc[[CALLSITE]] = loc(callsite(#loc{{[0-9]+}} at #loc{{[0-9]+}}))

// REMARK-DAG: not inlined: callee contains unknown obelisk_sim metadata
// REMARK-DAG: not inlined: display has no frozen lexical scope
// REMARK-DAG: not inlined: callee is not a defined zero-time function
// REMARK-DAG: not inlined: call is in a recursive SCC
// REMARK-DAG: not inlined: call boundary contains unknown obelisk_sim metadata
