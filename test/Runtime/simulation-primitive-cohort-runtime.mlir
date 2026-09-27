// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Five primitive actors are chunked 2+2+1.  The chain contains an internal
// dependency in each kernel, a dependency across both chunk boundaries, and
// a live one-member tail.  Both transitions must reach the last actor in both
// native and bytecode execution.
// CHECK: 11111
// CHECK: 00000

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @primitive_cohort_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "top.p0"
    obelisk_sim.code_unit.decl 3 in 0 continuous hierarchy "top.p1"
    obelisk_sim.code_unit.decl 4 in 0 continuous hierarchy "top.p2"
    obelisk_sim.code_unit.decl 5 in 0 continuous hierarchy "top.p3"
    obelisk_sim.code_unit.decl 6 in 0 continuous hierarchy "top.p4"
    obelisk_sim.code_unit.decl 7 in 0 initial hierarchy "top.check"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 5 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 2 in 0 drives 2 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 3 in 0 drives 3 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 4 in 0 drives 4 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 5 in 0 drives 5 : !obelisk_sim.logic<1> design

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %n0 = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n1 = obelisk_sim.context.net %ctx[1] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n2 = obelisk_sim.context.net %ctx[2] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n3 = obelisk_sim.context.net %ctx[3] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n4 = obelisk_sim.context.net %ctx[4] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n5 = obelisk_sim.context.net %ctx[5] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d2 = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d3 = obelisk_sim.context.driver %ctx[3] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d4 = obelisk_sim.context.driver %ctx[4] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d5 = obelisk_sim.context.driver %ctx[5] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %p0 = obelisk_sim.spawn @p0(%ctx, %n0, %d1) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p1 = obelisk_sim.spawn @p1(%ctx, %n1, %d2) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p2 = obelisk_sim.spawn @p2(%ctx, %n2, %d3) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p3 = obelisk_sim.spawn @p3(%ctx, %n3, %d4) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %p4 = obelisk_sim.spawn @p4(%ctx, %n4, %d5) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %check = obelisk_sim.spawn @check(%ctx, %d0, %n1, %n2, %n3, %n4, %n5) : !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @p0(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p1(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p2(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p3(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 3 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
    obelisk_sim.func private @p4(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 4 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %input : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %value : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.net<!obelisk_sim.logic<1>>
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %source: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %n1: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %n2: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %n3: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %n4: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 4 : i64},
        %n5: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 5 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.driver.drive %source = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.delay %delay to ^ones
    ^ones:
      %v1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v3 = obelisk_sim.net.read %n3 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v4 = obelisk_sim.net.read %n4 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v5 = obelisk_sim.net.read %n5 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format = obelisk_sim.bytes.constant "%b%b%b%b%b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %v1, %v2, %v3, %v4, %v5) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %source = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.suspend.delay %delay to ^zeros
    ^zeros:
      %w1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %w2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %w3 = obelisk_sim.net.read %n3 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %w4 = obelisk_sim.net.read %n4 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %w5 = obelisk_sim.net.read %n5 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.display %ctx to %stdout(%format, %w1, %w2, %w3, %w4, %w5) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
