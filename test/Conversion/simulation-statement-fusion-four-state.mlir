// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// Statement scheduling fuses the four-state producer, transform and consumer
// into one kernel in source order. Only the external input remains in its
// wait; the clocked observer stays a separate activation.
// CHECK-LABEL: simulation.func @root(
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0(
// CHECK: simulation.spawn @sample(
// CHECK-NOT: simulation.func @producer
// CHECK-NOT: simulation.func @transform
// CHECK-NOT: simulation.func @consumer
// CHECK-LABEL: simulation.func private @__obelisk_region_kernel_0_0(
// CHECK: %[[ONE:.*]] = simulation.logic.constant 1 : i8, 0 : i8
// CHECK: %[[INPUT:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[VALUE:.*]] = simulation.ref.load %[[INPUT]] {{.*}}codeUnit = 1
// CHECK-NEXT: simulation.ref.store %[[VALUE]] to %{{.*}} {{.*}}codeUnit = 1
// CHECK-NEXT: %[[NEXT:.*]] = simulation.logic.binary add %[[VALUE]], %[[ONE]]
// CHECK-NEXT: simulation.ref.store %[[NEXT]] to %{{.*}} {{.*}}codeUnit = 2
// CHECK-NEXT: simulation.ref.store %[[NEXT]] to %{{.*}} {{.*}}codeUnit = 1
// CHECK: simulation.suspend.any %[[INPUT]] edges [0] to

// Four-state producer/transform/consumer with an independent clocked observer.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @address_then_result {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.storage.decl 5 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "sample"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 5 in 0 always hierarchy "clock"
    simulation.code_unit.decl 6 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 6 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %c = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %s = simulation.spawn @sample(%ctx, %clock) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %d = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %a to %bank_address : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %r = simulation.ref.load %bank_result : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %r to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %r = simulation.logic.binary add %a, %one : !simulation.logic<8>
      simulation.ref.store %r to %result : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %address to ^body : !simulation.ref<!simulation.logic<8>>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %result = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %r = simulation.ref.load %result : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.nba.enqueue %r to %sample : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %delay = simulation.time.constant 1
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %fmt = simulation.bytes.constant "result %h sample %h"
      %channel = arith.constant 1 : i32
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^step0
    ^step0:
      %read0 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample0 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read0, %sample0) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value0 = simulation.logic.constant 7 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value0 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step1
    ^step1:
      %read1 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample1 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read1, %sample1) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value1 = simulation.logic.constant 0 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value1 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step2
    ^step2:
      %read2 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample2 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read2, %sample2) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value2 = simulation.logic.constant 255 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value2 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step3
    ^step3:
      %read3 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample3 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read3, %sample3) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value3 = simulation.logic.constant 255 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value3 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step4
    ^step4:
      %read4 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample4 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read4, %sample4) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %value4 = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value4 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step5
    ^step5:
      %read5 = simulation.ref.load %output : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %sample5 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %channel(%fmt, %read5, %sample5) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<8>, !simulation.logic<8>
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
