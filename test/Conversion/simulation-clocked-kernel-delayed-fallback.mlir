// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-clocked-kernels))' | FileCheck %s

// IEEE 1800-2023 4.9.4: a read after an NBA site of its root sees the
// incoming value. The kernel normally delays such updates to its end, keeping
// each site's value and target operands; a site that may run twice in one
// activation would need a queue, so its root reads a snapshot instead.

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
  // CHECK-LABEL: simulation.design @loop
  // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
  // CHECK: simulation.suspend.edge posedge %{{.*}} to ^[[START:bb[0-9]+]]
  // CHECK: ^[[START]]:
  // CHECK-NEXT: %[[OLD:.*]] = simulation.ref.load %[[ARRAY:arg[0-9]+]] : !simulation.ref<!simulation.unpacked_array
  // CHECK-NEXT: %[[SNAP:.*]] = simulation.ref.alloc %[[OLD]]
  // CHECK-NOT: arith.constant false
  // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<32>
  // CHECK: simulation.ref.array_element %[[SNAP]]
  // CHECK-NOT: simulation.nba.enqueue
  // CHECK: }
  simulation.design @loop {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : !ram design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !index design
    simulation.storage.decl 4 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "fill"
    simulation.code_unit.decl 5 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !clkref
      %array = simulation.context.storage %ctx[1] : !ramref
      %again = simulation.context.storage %ctx[2] : !bitref
      %a = simulation.context.storage %ctx[3] : !indexref
      %rv = simulation.context.storage %ctx[4] : !wordref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !clkref -> !simulation.process
      %p1 = simulation.spawn @fill(%ctx, %clk, %array, %again, %a, %rv) : !simulation.context, !clkref, !ramref, !bitref, !indexref, !wordref -> !simulation.process
      %p3 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
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
    simulation.func private @fill(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clkref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %array: !ramref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %again: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %a: !indexref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %rv: !wordref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %value = simulation.logic.constant 1 : i32, 0 : i32 : !word
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^loop : !clkref
    ^loop:
      %address = simulation.ref.load %a : !indexref -> !index
      %target = simulation.ref.array_element %array[%address] : (!ramref, !index) -> !wordref
      simulation.nba.enqueue %value to %target : (!word, !wordref) -> ()
      %flag = simulation.ref.load %again : !bitref -> !bit
      %repeat = simulation.logic.is_true %flag : !bit
      cf.cond_br %repeat, ^loop, ^sample
    ^sample:
      %read = simulation.ref.array_element %array[%address] : (!ramref, !index) -> !wordref
      %old = simulation.ref.load %read : !wordref -> !word
      simulation.nba.enqueue %old to %rv : (!word, !wordref) -> ()
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %array = simulation.context.storage %ctx[1] : !ramref
      %rv = simulation.context.storage %ctx[4] : !wordref
      %head = simulation.ref.subelement %array[[0]] : !ramref -> !wordref
      %first = simulation.ref.load %head : !wordref -> !word
      %sampled = simulation.ref.load %rv : !wordref -> !word
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d %d"
      simulation.display %ctx to %stdout(%format, %first, %sampled) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !word, !word
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
