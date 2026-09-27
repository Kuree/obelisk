// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true primitive-only=true max-straight-line-members=2},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  simulation.design @primitive_cohort_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "top.p0"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "top.p1"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "top.p2"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "top.p3"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "top.p4"
    simulation.code_unit.decl 7 in 0 initial hierarchy "top.check"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.decl 2 in 0 : !simulation.logic<1> design
    simulation.net.decl 3 in 0 : !simulation.logic<1> design
    simulation.net.decl 4 in 0 : !simulation.logic<1> design
    simulation.net.decl 5 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<1> design
    simulation.driver.decl 2 in 0 drives 2 : !simulation.logic<1> design
    simulation.driver.decl 3 in 0 drives 3 : !simulation.logic<1> design
    simulation.driver.decl 4 in 0 drives 4 : !simulation.logic<1> design
    simulation.driver.decl 5 in 0 drives 5 : !simulation.logic<1> design

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %n0 = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %n1 = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %n2 = simulation.context.net %ctx[2] : !simulation.net<!simulation.logic<1>>
      %n3 = simulation.context.net %ctx[3] : !simulation.net<!simulation.logic<1>>
      %n4 = simulation.context.net %ctx[4] : !simulation.net<!simulation.logic<1>>
      %n5 = simulation.context.net %ctx[5] : !simulation.net<!simulation.logic<1>>
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %d2 = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %d3 = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %d4 = simulation.context.driver %ctx[4] : !simulation.driver<!simulation.logic<1>>
      %d5 = simulation.context.driver %ctx[5] : !simulation.driver<!simulation.logic<1>>
      %p0 = simulation.spawn @p0(%ctx, %n0, %d1) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p1 = simulation.spawn @p1(%ctx, %n1, %d2) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p2 = simulation.spawn @p2(%ctx, %n2, %d3) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p3 = simulation.spawn @p3(%ctx, %n3, %d4) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %p4 = simulation.spawn @p4(%ctx, %n4, %d5) : !simulation.context, !simulation.net<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>> -> !simulation.process
      %check = simulation.spawn @check(%ctx, %d0, %n1, %n2, %n3, %n4, %n5) : !simulation.context, !simulation.driver<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @p0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }
    simulation.func private @p1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }
    simulation.func private @p2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }
    simulation.func private @p3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }
    simulation.func private @p4(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 4 : i64}, %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64, schedule.primitive_name = "buf"} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %input : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.change %input to ^body : !simulation.net<!simulation.logic<1>>
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %source: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %n1: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64},
        %n2: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64},
        %n3: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64},
        %n4: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 4 : i64},
        %n5: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 5 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %delay = simulation.time.constant 1
      simulation.driver.drive %source = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.delay %delay to ^ones
    ^ones:
      %v1 = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v2 = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v3 = simulation.net.read %n3 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v4 = simulation.net.read %n4 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v5 = simulation.net.read %n5 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "%b%b%b%b%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %v1, %v2, %v3, %v4, %v5) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>
      simulation.driver.drive %source = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.suspend.delay %delay to ^zeros
    ^zeros:
      %w1 = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %w2 = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %w3 = simulation.net.read %n3 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %w4 = simulation.net.read %n4 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %w5 = simulation.net.read %n5 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.display %ctx to %stdout(%format, %w1, %w2, %w3, %w4, %w5) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>, !simulation.logic<1>
      simulation.return
    }
  }
}
