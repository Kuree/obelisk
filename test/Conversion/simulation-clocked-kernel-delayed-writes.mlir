// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-fuse-clocked-kernels))' | FileCheck %s

// A true dual-port RAM: each port is its own always_ff, so in the kernel the
// second port reads `array` after the first port's NBA site. That read must
// see the incoming value (IEEE 1800-2023 4.9.4). Instead of copying the whole
// array, each site keeps its value and index (evaluated at the statement) and
// sets a flag, and the kernel applies the flagged updates after its last
// member in execution order (4.6(b)), so the second port's write wins when
// both address the same element.

!bit = !simulation.logic<1>
!index = !simulation.logic<6>
!word = !simulation.logic<32>
!ram = !simulation.unpacked_array<0 : 63 x !word>
!bitref = !simulation.ref<!bit>
!clkref = !simulation.ref<i1>
!indexref = !simulation.ref<!index>
!wordref = !simulation.ref<!word>
!ramref = !simulation.ref<!ram>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @ram {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !ram design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !index design
    simulation.storage.decl 5 in 0 : !index design
    simulation.storage.decl 6 in 0 : !word design
    simulation.storage.decl 7 in 0 : !word design
    simulation.storage.decl 8 in 0 : !word design
    simulation.storage.decl 9 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "port0"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "port1"
    simulation.code_unit.decl 5 in 0 initial hierarchy "drive"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clkref
      %array = simulation.context.storage %ctx[1] : !ramref
      %we0 = simulation.context.storage %ctx[2] : !bitref
      %we1 = simulation.context.storage %ctx[3] : !bitref
      %a0 = simulation.context.storage %ctx[4] : !indexref
      %a1 = simulation.context.storage %ctx[5] : !indexref
      %wv0 = simulation.context.storage %ctx[6] : !wordref
      %wv1 = simulation.context.storage %ctx[7] : !wordref
      %rv0 = simulation.context.storage %ctx[8] : !wordref
      %rv1 = simulation.context.storage %ctx[9] : !wordref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clkref -> !simulation.process
      %p1 = simulation.spawn @port(%ctx, %clk, %array, %we0, %a0, %wv0, %rv0) : !simulation.context, !clkref, !ramref, !bitref, !indexref, !wordref, !wordref -> !simulation.process
      %p2 = simulation.spawn @port_1(%ctx, %clk, %array, %we1, %a1, %wv1, %rv1) : !simulation.context, !clkref, !ramref, !bitref, !indexref, !wordref, !wordref -> !simulation.process
      %zero = simulation.logic.constant 0 : i32, 0 : i32 : !word
      %zeros = simulation.aggregate.splat %zero : (!word) -> !ram
      simulation.ref.store %zeros to %array : !ram, !ramref
      %p3 = simulation.spawn @drive(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clkref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = arith.constant true
      %zero = arith.constant false
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (i1, !clkref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (i1, !clkref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @ram
    // CHECK-LABEL: simulation.func private @__talos_clocked_0_1_0
    // CHECK-SAME: %[[ARRAY:arg2]]: !simulation.ref<!simulation.unpacked_array<0 : 63 x !simulation.logic<32>>>
    // CHECK: simulation.suspend.edge posedge %{{.*}} to ^[[START:bb[0-9]+]]
    // CHECK: ^[[START]]:
    // CHECK-NEXT: %[[UNSET:.*]] = arith.constant false
    // CHECK-NEXT: %[[FLAG0:.*]] = simulation.ref.alloc %[[UNSET]] : i1
    // CHECK: %[[VALUE0:.*]] = simulation.ref.alloc %{{.*}} : !simulation.logic<32>
    // CHECK: %[[INDEX0:.*]] = simulation.ref.alloc %{{.*}} : !simulation.logic<6>
    // CHECK: %[[FLAG1:.*]] = simulation.ref.alloc %[[UNSET]] : i1
    // CHECK: %[[VALUE1:.*]] = simulation.ref.alloc %{{.*}} : !simulation.logic<32>
    // CHECK: %[[INDEX1:.*]] = simulation.ref.alloc %{{.*}} : !simulation.logic<6>
    // CHECK-NOT: simulation.ref.load %[[ARRAY]] :
    // CHECK: %[[ADDRESS0:.*]] = simulation.ref.load %{{.*}} : !simulation.ref<!simulation.logic<6>>
    // CHECK: cf.cond_br %{{.*}}, ^[[STORE0:bb[0-9]+]], ^{{bb[0-9]+}}
    // CHECK: ^[[STORE0]]:
    // CHECK-NEXT: %[[WRITE0:.*]] = simulation.ref.load
    // CHECK-NEXT: simulation.ref.array_element
    // CHECK-NEXT: simulation.ref.store %[[WRITE0]] to %[[VALUE0]]
    // CHECK-NEXT: simulation.ref.store %[[ADDRESS0]] to %[[INDEX0]]
    // CHECK-NEXT: %[[SET0:.*]] = arith.constant true
    // CHECK-NEXT: simulation.ref.store %[[SET0]] to %[[FLAG0]]
    // The second port reads the array itself, which still holds the incoming
    // value.
    // CHECK: %[[ADDRESS1:.*]] = simulation.ref.load %{{.*}} : !simulation.ref<!simulation.logic<6>>
    // CHECK-NEXT: %[[LATE:.*]] = simulation.ref.array_element %[[ARRAY]][%[[ADDRESS1]]]
    // CHECK-NEXT: simulation.ref.load %[[LATE]]
    // CHECK: simulation.ref.store %{{.*}} to %[[FLAG1]]
    // CHECK-NEXT: cf.br ^[[COMMIT:bb[0-9]+]]
    // CHECK: ^[[COMMIT]]:
    // CHECK-NEXT: %[[IS0:.*]] = simulation.ref.load %[[FLAG0]]
    // CHECK-NEXT: cf.cond_br %[[IS0]], ^[[APPLY0:bb[0-9]+]], ^[[NEXT0:bb[0-9]+]]
    // CHECK: ^[[APPLY0]]:
    // CHECK-NEXT: %[[V0:.*]] = simulation.ref.load %[[VALUE0]]
    // CHECK-NEXT: %[[I0:.*]] = simulation.ref.load %[[INDEX0]]
    // CHECK-NEXT: %[[T0:.*]] = simulation.ref.array_element %[[ARRAY]][%[[I0]]]
    // CHECK-NEXT: simulation.ref.store %[[V0]] to %[[T0]]
    // CHECK: ^[[NEXT0]]:
    // CHECK-NEXT: %[[IS1:.*]] = simulation.ref.load %[[FLAG1]]
    // CHECK: simulation.ref.load %[[VALUE1]]
    // CHECK-NEXT: %[[I1:.*]] = simulation.ref.load %[[INDEX1]]
    // CHECK-NEXT: %[[T1:.*]] = simulation.ref.array_element %[[ARRAY]][%[[I1]]]
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %[[T1]]
    // CHECK: cf.br ^bb1
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: }
    simulation.func private @port(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clkref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %array: !ramref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %we: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %a: !indexref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %wv: !wordref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %rv: !wordref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clkref
    ^body:
      %address = simulation.ref.load %a : !indexref -> !index
      %read = simulation.ref.array_element %array[%address] : (!ramref, !index) -> !wordref
      %old = simulation.ref.load %read : !wordref -> !word
      simulation.nba.enqueue %old to %rv : (!word, !wordref) -> ()
      %enable = simulation.ref.load %we : !bitref -> !bit
      %write = simulation.logic.is_true %enable : !bit
      cf.cond_br %write, ^store, ^wait
    ^store:
      %value = simulation.ref.load %wv : !wordref -> !word
      %target = simulation.ref.array_element %array[%address] : (!ramref, !index) -> !wordref
      simulation.nba.enqueue %value to %target : (!word, !wordref) -> ()
      cf.br ^wait
    }
    simulation.func private @port_1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clkref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %array: !ramref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %we: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %a: !indexref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}, %wv: !wordref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}, %rv: !wordref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clkref
    ^body:
      %address = simulation.ref.load %a : !indexref -> !index
      %read = simulation.ref.array_element %array[%address] : (!ramref, !index) -> !wordref
      %old = simulation.ref.load %read : !wordref -> !word
      simulation.nba.enqueue %old to %rv : (!word, !wordref) -> ()
      %enable = simulation.ref.load %we : !bitref -> !bit
      %write = simulation.logic.is_true %enable : !bit
      cf.cond_br %write, ^store, ^wait
    ^store:
      %value = simulation.ref.load %wv : !wordref -> !word
      %target = simulation.ref.array_element %array[%address] : (!ramref, !index) -> !wordref
      simulation.nba.enqueue %value to %target : (!word, !wordref) -> ()
      cf.br ^wait
    }
    // Edges at 5, 15 and 25. The first writes 11 to element 3. At the second
    // both ports write element 3, and the second port also reads it.
    simulation.func @drive(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %array = simulation.context.storage %ctx[1] : !ramref
      %we0 = simulation.context.storage %ctx[2] : !bitref
      %we1 = simulation.context.storage %ctx[3] : !bitref
      %a0 = simulation.context.storage %ctx[4] : !indexref
      %a1 = simulation.context.storage %ctx[5] : !indexref
      %wv0 = simulation.context.storage %ctx[6] : !wordref
      %wv1 = simulation.context.storage %ctx[7] : !wordref
      %rv0 = simulation.context.storage %ctx[8] : !wordref
      %rv1 = simulation.context.storage %ctx[9] : !wordref
      %on = simulation.logic.constant true, false : !bit
      %off = simulation.logic.constant false, false : !bit
      %three = simulation.logic.constant 3 : i6, 0 : i6 : !index
      %five = simulation.logic.constant 5 : i6, 0 : i6 : !index
      %v11 = simulation.logic.constant 11 : i32, 0 : i32 : !word
      %v22 = simulation.logic.constant 22 : i32, 0 : i32 : !word
      %v33 = simulation.logic.constant 33 : i32, 0 : i32 : !word
      %delay = simulation.time.constant 10
      %stdout = arith.constant -2147483647 : i32
      simulation.ref.store %on to %we0 : !bit, !bitref
      simulation.ref.store %three to %a0 : !index, !indexref
      simulation.ref.store %v11 to %wv0 : !word, !wordref
      simulation.ref.store %off to %we1 : !bit, !bitref
      simulation.ref.store %three to %a1 : !index, !indexref
      simulation.suspend.delay %delay to ^second
    ^second:
      simulation.ref.store %v22 to %wv0 : !word, !wordref
      simulation.ref.store %on to %we1 : !bit, !bitref
      simulation.ref.store %v33 to %wv1 : !word, !wordref
      simulation.suspend.delay %delay to ^third
    ^third:
      simulation.ref.store %off to %we0 : !bit, !bitref
      simulation.ref.store %off to %we1 : !bit, !bitref
      simulation.ref.store %five to %a1 : !index, !indexref
      %r0 = simulation.ref.load %rv0 : !wordref -> !word
      %r1 = simulation.ref.load %rv1 : !wordref -> !word
      %pair = simulation.bytes.constant "%0d %0d"
      simulation.display %ctx to %stdout(%pair, %r0, %r1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      simulation.suspend.delay %delay to ^fourth
    ^fourth:
      %s0 = simulation.ref.load %rv0 : !wordref -> !word
      %s1 = simulation.ref.load %rv1 : !wordref -> !word
      %cell = simulation.ref.subelement %array[[3]] : !ramref -> !wordref
      %c = simulation.ref.load %cell : !wordref -> !word
      %triple = simulation.bytes.constant "%0d %0d %0d"
      simulation.display %ctx to %stdout(%triple, %s0, %s1, %c) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, !word, !word, !word
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
