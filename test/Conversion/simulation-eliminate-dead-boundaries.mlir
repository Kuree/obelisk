// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries,obelisk_sim.func(canonicalize,cse)))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries,obelisk_sim.func(canonicalize,cse)))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s < %t.threaded
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries{missed-remarks=true}))' -o /dev/null 2> %t.remarks
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries{missed-remarks=true}))' -o /dev/null 2> %t.serial-remarks
// RUN: diff -u %t.serial-remarks %t.remarks
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s --check-prefix=LOC

module {
  obelisk_sim.design @boundaries {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.partial"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.pure_math"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.pure_reads"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "top.writer"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "top.cycle_a"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "top.cycle_b"
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "top.spawning"
    obelisk_sim.code_unit.decl 8 in 0 initial hierarchy "top.child"
    obelisk_sim.code_unit.decl 9 in 0 function hierarchy "top.partial_caller"
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "top.multi_return"
    obelisk_sim.storage.decl 0 in 0 : i32 design
    obelisk_sim.net.decl 0 in 0 : i32 design

    // Only the middle result is demanded. Its metadata and SSA uses move to
    // result position zero; the dead input and the other return operands go.
    // CHECK-LABEL: obelisk_sim.func private @partial(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-NOT: %arg1
    // CHECK-SAME: -> (i32 {test.result = "middle"})
    // CHECK: obelisk_sim.return %{{.*}} : i32
    obelisk_sim.func private @partial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %initial: i32 {obelisk_sim.capture_kind = 1 : i32})
        -> (i8 {test.result = "first"}, i32 {test.result = "middle"},
            i64 {test.result = "last"})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %first = arith.constant 1 : i8
      %middle = arith.constant 2 : i32
      %last = arith.constant 3 : i64
      obelisk_sim.return %first, %middle, %last : i8, i32, i64
    } loc("boundary.sv":4:3)

    // Pure arithmetic and storage/net reads are discardable when their call
    // results have no demand.
    // CHECK-LABEL: obelisk_sim.func private @pure_math(
    // CHECK-SAME: %arg1: i32
    // CHECK-SAME: ) attributes
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @pure_math(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      obelisk_sim.return %sum : i32
    }

    // CHECK-LABEL: obelisk_sim.func private @pure_reads(
    // CHECK-SAME: %arg1: !obelisk_sim.ref<i32>
    // CHECK-SAME: %arg2: !obelisk_sim.net<i32>
    // CHECK-SAME: ) attributes
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @pure_reads(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %storage: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<i32> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %stored = obelisk_sim.ref.load %storage : !obelisk_sim.ref<i32> -> i32
      %driven = obelisk_sim.net.read %net : !obelisk_sim.net<i32> -> i32
      %sum = arith.addi %stored, %driven : i32
      obelisk_sim.return %sum : i32
    }

    // A storage write keeps the call active, but its unused copy-out result is
    // still removed at the function, return, and call boundaries.
    // CHECK-LABEL: obelisk_sim.func private @writer(
    // CHECK-SAME: %arg2: i32
    // CHECK-SAME: ) attributes
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @writer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %storage: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32})
        -> (i32 {test.copy_out = true})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.ref.store %value to %storage : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return %value : i32
    }

    // A pure recursive forwarding SCC has no semantic demand seed.
    // CHECK-LABEL: obelisk_sim.func private @cycle_a(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-NOT: %arg1
    // CHECK-NOT: obelisk_sim.call
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @cycle_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %result = obelisk_sim.call @cycle_b(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // CHECK-LABEL: obelisk_sim.func private @cycle_b(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-NOT: %arg1
    // CHECK-NOT: obelisk_sim.call
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @cycle_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %result = obelisk_sim.call @cycle_a(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // Spawn/scheduler effects make a zero-time function non-discardable.
    // CHECK-LABEL: obelisk_sim.func private @spawning(
    // CHECK: obelisk_sim.spawn @child
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @spawning(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %child = obelisk_sim.spawn @child(%ctx)
          : !obelisk_sim.context -> !obelisk_sim.process
      %value = arith.constant 9 : i32
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      obelisk_sim.return
    }

    // A second caller demands the same surviving result through a direct
    // result/return forwarding chain. Fixed-point propagation must rebuild
    // this call to @partial identically to the root call.
    // CHECK-LABEL: obelisk_sim.func private @partial_caller(
    // CHECK: %[[FORWARDED:.*]] = obelisk_sim.call @partial(%arg0) : (!obelisk_sim.context) -> i32
    // CHECK: obelisk_sim.return %[[FORWARDED]] : i32
    obelisk_sim.func private @partial_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %zero = arith.constant 0 : i32
      %a, %middle, %c = obelisk_sim.call @partial(%ctx, %zero)
          : (!obelisk_sim.context, i32) -> (i8, i32, i64) loc("boundary.sv":40:7)
      obelisk_sim.return %middle : i32
    }

    // Every return in a multi-block function is filtered in lockstep. The
    // condition is an ordinary semantic consumer; the unrelated input dies.
    // CHECK-LABEL: obelisk_sim.func private @multi_return(
    // CHECK-SAME: %arg1: i1
    // CHECK-NOT: %arg2
    // CHECK-SAME: -> i32
    // CHECK: ^bb1:
    // CHECK: obelisk_sim.return %{{.*}} : i32
    // CHECK: ^bb2:
    // CHECK: obelisk_sim.return %{{.*}} : i32
    obelisk_sim.func private @multi_return(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: i1 {obelisk_sim.capture_kind = 1 : i32},
        %dead: i64 {obelisk_sim.capture_kind = 1 : i32}) -> (i8, i32)
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      cf.cond_br %condition, ^bb1, ^bb2
    ^bb1:
      %a = arith.constant 10 : i8
      %b = arith.constant 11 : i32
      obelisk_sim.return %a, %b : i8, i32
    ^bb2:
      %c = arith.constant 12 : i8
      %d = arith.constant 13 : i32
      obelisk_sim.return %c, %d : i8, i32
    }

    // CHECK-LABEL: obelisk_sim.func @root(
    // CHECK: %[[MIDDLE:.*]] = obelisk_sim.call @partial(%arg0)
    // CHECK-SAME: arg_attrs = [{test.call_arg = "context"}]
    // CHECK-SAME: res_attrs = [{test.call_result = "middle"}]
    // CHECK-SAME: test.keep = true
    // CHECK-SAME: : (!obelisk_sim.context) -> i32
    // CHECK: obelisk_sim.ref.store %[[MIDDLE]]
    // CHECK: %[[FORWARDED_ROOT:.*]] = obelisk_sim.call @partial_caller(%arg0) : (!obelisk_sim.context) -> i32
    // CHECK: obelisk_sim.ref.store %[[FORWARDED_ROOT]]
    // CHECK: %[[MULTI:.*]] = obelisk_sim.call @multi_return(%arg0, %{{.*}}) : (!obelisk_sim.context, i1) -> i32
    // CHECK: obelisk_sim.ref.store %[[MULTI]]
    // CHECK-NOT: obelisk_sim.call @pure_math
    // CHECK-NOT: obelisk_sim.call @pure_reads
    // CHECK: obelisk_sim.call @writer{{.*}} : (!obelisk_sim.context, !obelisk_sim.ref<i32>, i32) -> ()
    // CHECK-NOT: obelisk_sim.call @cycle_a
    // CHECK: obelisk_sim.call @spawning(%arg0) : (!obelisk_sim.context) -> ()
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = arith.constant 0 : i32
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<i32>
      %a, %middle, %c = obelisk_sim.call @partial(%ctx, %zero)
          {arg_attrs = [{test.call_arg = "context"}, {test.call_arg = "dead"}],
           res_attrs = [{test.call_result = "first"},
                        {test.call_result = "middle"},
                        {test.call_result = "last"}], test.keep = true}
          : (!obelisk_sim.context, i32) -> (i8, i32, i64)
          loc("boundary.sv":60:9)
      obelisk_sim.ref.store %middle to %storage : i32, !obelisk_sim.ref<i32>
      %forwarded_middle = obelisk_sim.call @partial_caller(%ctx)
          : (!obelisk_sim.context) -> i32
      obelisk_sim.ref.store %forwarded_middle to %storage : i32, !obelisk_sim.ref<i32>
      %condition = arith.constant true
      %first, %multi = obelisk_sim.call @multi_return(%ctx, %condition, %c)
          : (!obelisk_sim.context, i1, i64) -> (i8, i32)
      obelisk_sim.ref.store %multi to %storage : i32, !obelisk_sim.ref<i32>
      %math = obelisk_sim.call @pure_math(%ctx, %zero)
          : (!obelisk_sim.context, i32) -> i32
      %read = obelisk_sim.call @pure_reads(%ctx, %storage, %net)
          : (!obelisk_sim.context, !obelisk_sim.ref<i32>, !obelisk_sim.net<i32>) -> i32
      %written = obelisk_sim.call @writer(%ctx, %storage, %zero)
          : (!obelisk_sim.context, !obelisk_sim.ref<i32>, i32) -> i32
      %cycle = obelisk_sim.call @cycle_a(%ctx, %zero)
          : (!obelisk_sim.context, i32) -> i32
      %spawned = obelisk_sim.call @spawning(%ctx)
          : (!obelisk_sim.context) -> i32
      obelisk_sim.return
    }
  }
}

// REMARK: dead boundary elimination retained ABI: root initializer ABI

// LOC: obelisk_sim.func private @partial
// LOC: } loc(#[[FUNCTION:loc[0-9]+]])
// LOC: obelisk_sim.call @partial
// LOC-SAME: loc(#[[CALL:loc[0-9]+]])
// LOC: obelisk_sim.call @partial
// LOC-SAME: arg_attrs = [{test.call_arg = "context"}]
// LOC-SAME: res_attrs = [{test.call_result = "middle"}]
// LOC-SAME: test.keep = true
// LOC-SAME: loc(#[[ATTR_CALL:loc[0-9]+]])
// LOC: #[[FUNCTION]] = loc("boundary.sv":4:3)
// LOC: #[[CALL]] = loc("boundary.sv":40:7)
// LOC: #[[ATTR_CALL]] = loc("boundary.sv":60:9)
