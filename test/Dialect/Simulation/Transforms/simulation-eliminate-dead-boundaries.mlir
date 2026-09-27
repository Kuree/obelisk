// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries,simulation.func(canonicalize,cse)))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries,simulation.func(canonicalize,cse)))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s < %t.threaded
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries{missed-remarks=true}))' -o /dev/null 2> %t.remarks
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries{missed-remarks=true}))' -o /dev/null 2> %t.serial-remarks
// RUN: diff -u %t.serial-remarks %t.remarks
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s --check-prefix=LOC

module {
  simulation.design @boundaries {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.partial"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.pure_math"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.pure_reads"
    simulation.code_unit.decl 4 in 0 function hierarchy "top.writer"
    simulation.code_unit.decl 5 in 0 function hierarchy "top.cycle_a"
    simulation.code_unit.decl 6 in 0 function hierarchy "top.cycle_b"
    simulation.code_unit.decl 7 in 0 function hierarchy "top.spawning"
    simulation.code_unit.decl 8 in 0 initial hierarchy "top.child"
    simulation.code_unit.decl 9 in 0 function hierarchy "top.partial_caller"
    simulation.code_unit.decl 10 in 0 function hierarchy "top.multi_return"
    simulation.storage.decl 0 in 0 : i32 design
    simulation.net.decl 0 in 0 : i32 design

    // Only the middle result is demanded. Its metadata and SSA uses move to
    // result position zero; the dead input and the other return operands go.
    // CHECK-LABEL: simulation.func private @partial(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-NOT: %arg1
    // CHECK-SAME: -> (i32 {test.result = "middle"})
    // CHECK: simulation.return %{{.*}} : i32
    simulation.func private @partial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i32 {simulation.capture_kind = 1 : i32})
        -> (i8 {test.result = "first"}, i32 {test.result = "middle"},
            i64 {test.result = "last"})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %first = arith.constant 1 : i8
      %middle = arith.constant 2 : i32
      %last = arith.constant 3 : i64
      simulation.return %first, %middle, %last : i8, i32, i64
    } loc("boundary.sv":4:3)

    // Pure arithmetic and storage/net reads are discardable when their call
    // results have no demand.
    // CHECK-LABEL: simulation.func private @pure_math(
    // CHECK-SAME: %arg1: i32
    // CHECK-SAME: ) attributes
    // CHECK: simulation.return
    simulation.func private @pure_math(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      simulation.return %sum : i32
    }

    // CHECK-LABEL: simulation.func private @pure_reads(
    // CHECK-SAME: %arg1: !simulation.ref<i32>
    // CHECK-SAME: %arg2: !simulation.net<i32>
    // CHECK-SAME: ) attributes
    // CHECK: simulation.return
    simulation.func private @pure_reads(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<i32> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %stored = simulation.ref.load %storage : !simulation.ref<i32> -> i32
      %driven = simulation.net.read %net : !simulation.net<i32> -> i32
      %sum = arith.addi %stored, %driven : i32
      simulation.return %sum : i32
    }

    // A storage write keeps the call active, but its unused copy-out result is
    // still removed at the function, return, and call boundaries.
    // CHECK-LABEL: simulation.func private @writer(
    // CHECK-SAME: %arg2: i32
    // CHECK-SAME: ) attributes
    // CHECK: simulation.ref.store
    // CHECK: simulation.return
    simulation.func private @writer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %value: i32 {simulation.capture_kind = 1 : i32})
        -> (i32 {test.copy_out = true})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.ref.store %value to %storage : i32, !simulation.ref<i32>
      simulation.return %value : i32
    }

    // Recursive calls may not return. Remove dead ABI positions, but retain
    // the calls themselves (IEEE 1800-2023 13.4 and 12.7.6).
    // CHECK-LABEL: simulation.func private @cycle_a(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-NOT: %arg1
    // CHECK: simulation.call
    // CHECK: simulation.return
    simulation.func private @cycle_a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %result = simulation.call @cycle_b(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // CHECK-LABEL: simulation.func private @cycle_b(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-NOT: %arg1
    // CHECK: simulation.call
    // CHECK: simulation.return
    simulation.func private @cycle_b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %result = simulation.call @cycle_a(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // Spawn/scheduler effects make a zero-time function non-discardable.
    // CHECK-LABEL: simulation.func private @spawning(
    // CHECK: simulation.spawn @child
    // CHECK: simulation.return
    simulation.func private @spawning(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %child = simulation.spawn @child(%ctx)
          : !simulation.context -> !simulation.process
      %value = arith.constant 9 : i32
      simulation.return %value : i32
    }

    simulation.func private @child(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      simulation.return
    }

    // A second caller demands the same surviving result through a direct
    // result/return forwarding chain. Fixed-point propagation must rebuild
    // this call to @partial identically to the root call.
    // CHECK-LABEL: simulation.func private @partial_caller(
    // CHECK: %[[FORWARDED:.*]] = simulation.call @partial(%arg0) : (!simulation.context) -> i32
    // CHECK: simulation.return %[[FORWARDED]] : i32
    simulation.func private @partial_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %zero = arith.constant 0 : i32
      %a, %middle, %c = simulation.call @partial(%ctx, %zero)
          : (!simulation.context, i32) -> (i8, i32, i64) loc("boundary.sv":40:7)
      simulation.return %middle : i32
    }

    // Every return in a multi-block function is filtered in lockstep. The
    // condition is an ordinary semantic consumer; the unrelated input dies.
    // CHECK-LABEL: simulation.func private @multi_return(
    // CHECK-SAME: %arg1: i1
    // CHECK-NOT: %arg2
    // CHECK-SAME: -> i32
    // CHECK: ^bb1:
    // CHECK: simulation.return %{{.*}} : i32
    // CHECK: ^bb2:
    // CHECK: simulation.return %{{.*}} : i32
    simulation.func private @multi_return(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: i1 {simulation.capture_kind = 1 : i32},
        %dead: i64 {simulation.capture_kind = 1 : i32}) -> (i8, i32)
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      cf.cond_br %condition, ^bb1, ^bb2
    ^bb1:
      %a = arith.constant 10 : i8
      %b = arith.constant 11 : i32
      simulation.return %a, %b : i8, i32
    ^bb2:
      %c = arith.constant 12 : i8
      %d = arith.constant 13 : i32
      simulation.return %c, %d : i8, i32
    }

    // CHECK-LABEL: simulation.func @root(
    // CHECK: %[[MIDDLE:.*]] = simulation.call @partial(%arg0)
    // CHECK-SAME: arg_attrs = [{test.call_arg = "context"}]
    // CHECK-SAME: res_attrs = [{test.call_result = "middle"}]
    // CHECK-SAME: test.keep = true
    // CHECK-SAME: : (!simulation.context) -> i32
    // CHECK: simulation.ref.store %[[MIDDLE]]
    // CHECK: %[[FORWARDED_ROOT:.*]] = simulation.call @partial_caller(%arg0) : (!simulation.context) -> i32
    // CHECK: simulation.ref.store %[[FORWARDED_ROOT]]
    // CHECK: %[[MULTI:.*]] = simulation.call @multi_return(%arg0, %{{.*}}) : (!simulation.context, i1) -> i32
    // CHECK: simulation.ref.store %[[MULTI]]
    // CHECK-NOT: simulation.call @pure_math
    // CHECK-NOT: simulation.call @pure_reads
    // CHECK: simulation.call @writer{{.*}} : (!simulation.context, !simulation.ref<i32>, i32) -> ()
    // CHECK: simulation.call @cycle_a
    // CHECK: simulation.call @spawning(%arg0) : (!simulation.context) -> ()
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %net = simulation.context.net %ctx[0] : !simulation.net<i32>
      %a, %middle, %c = simulation.call @partial(%ctx, %zero)
          {arg_attrs = [{test.call_arg = "context"}, {test.call_arg = "dead"}],
           res_attrs = [{test.call_result = "first"},
                        {test.call_result = "middle"},
                        {test.call_result = "last"}], test.keep = true}
          : (!simulation.context, i32) -> (i8, i32, i64)
          loc("boundary.sv":60:9)
      simulation.ref.store %middle to %storage : i32, !simulation.ref<i32>
      %forwarded_middle = simulation.call @partial_caller(%ctx)
          : (!simulation.context) -> i32
      simulation.ref.store %forwarded_middle to %storage : i32, !simulation.ref<i32>
      %condition = arith.constant true
      %first, %multi = simulation.call @multi_return(%ctx, %condition, %c)
          : (!simulation.context, i1, i64) -> (i8, i32)
      simulation.ref.store %multi to %storage : i32, !simulation.ref<i32>
      %math = simulation.call @pure_math(%ctx, %zero)
          : (!simulation.context, i32) -> i32
      // Keep several independent inactive calls in one erase batch. The
      // implementation must not retain invalidated vector iterators while
      // removing them in reverse order.
      %math1 = simulation.call @pure_math(%ctx, %zero)
          : (!simulation.context, i32) -> i32
      %math2 = simulation.call @pure_math(%ctx, %zero)
          : (!simulation.context, i32) -> i32
      %math3 = simulation.call @pure_math(%ctx, %zero)
          : (!simulation.context, i32) -> i32
      %read = simulation.call @pure_reads(%ctx, %storage, %net)
          : (!simulation.context, !simulation.ref<i32>, !simulation.net<i32>) -> i32
      %written = simulation.call @writer(%ctx, %storage, %zero)
          : (!simulation.context, !simulation.ref<i32>, i32) -> i32
      %cycle = simulation.call @cycle_a(%ctx, %zero)
          : (!simulation.context, i32) -> i32
      %spawned = simulation.call @spawning(%ctx)
          : (!simulation.context) -> i32
      simulation.return
    }
  }
}

// REMARK: dead boundary elimination retained ABI: root initializer ABI

// LOC: simulation.func private @partial
// LOC: } loc(#[[FUNCTION:loc[0-9]+]])
// LOC: simulation.call @partial
// LOC-SAME: loc(#[[CALL:loc[0-9]+]])
// LOC: simulation.call @partial
// LOC-SAME: arg_attrs = [{test.call_arg = "context"}]
// LOC-SAME: res_attrs = [{test.call_result = "middle"}]
// LOC-SAME: test.keep = true
// LOC-SAME: loc(#[[ATTR_CALL:loc[0-9]+]])
// LOC: #[[FUNCTION]] = loc("boundary.sv":4:3)
// LOC: #[[CALL]] = loc("boundary.sv":40:7)
// LOC: #[[ATTR_CALL]] = loc("boundary.sv":60:9)
