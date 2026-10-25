// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-clocked-kernels))' -o %t.mlir 2> %t.log
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=REPORT < %t.log

// Where clocked kernels stop. Each case states the IEEE 1800-2023 rule that
// keeps an NBA, or a member out of a kernel.

// Two independent clocks that rise in the same slot give two kernels; each
// one's edge wakes the other's processes in the same Active region (4.5).
// `cross` reads `a`, which the other clock's kernel writes, so it must still
// see the old `a` (4.9.4): `a` keeps its NBA. `b` and `c` are read only after
// a delay and become stores.

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {native.debug_timing} {
  simulation.design @two_clocks {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.storage.decl 4 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clocks"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "cross"
    simulation.code_unit.decl 5 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk0 = simulation.context.storage %ctx[0] : !bitref
      %clk1 = simulation.context.storage %ctx[1] : !bitref
      %a = simulation.context.storage %ctx[2] : !byteref
      %b = simulation.context.storage %ctx[3] : !byteref
      %c = simulation.context.storage %ctx[4] : !byteref
      %p0 = simulation.spawn @clocks(%ctx, %clk0, %clk1) : !simulation.context, !bitref, !bitref -> !simulation.process
      %p1 = simulation.spawn @count(%ctx, %clk0, %a, %c) : !simulation.context, !bitref, !byteref, !byteref -> !simulation.process
      %p2 = simulation.spawn @cross(%ctx, %clk1, %a, %b) : !simulation.context, !bitref, !byteref, !byteref -> !simulation.process
      %p3 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clocks(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk0: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %clk1: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk0 : (!bit, !bitref) -> ()
      simulation.nba.enqueue %one to %clk1 : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk0 : (!bit, !bitref) -> ()
      simulation.nba.enqueue %zero to %clk1 : (!bit, !bitref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @two_clocks
    // CHECK-DAG: simulation.spawn @__obelisk_clocked_0_1_0(
    // CHECK-DAG: simulation.spawn @__obelisk_clocked_1_1_0(
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: %[[NEXT:.*]] = simulation.logic.binary add
    // CHECK-NEXT: simulation.nba.enqueue %[[NEXT]]
    // CHECK-NEXT: simulation.ref.store %[[NEXT]]
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_1_1_0
    // CHECK: %[[A:.*]] = simulation.ref.load
    // CHECK-NEXT: simulation.ref.store %[[A]]
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: cf.br
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=1 retained=0
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_1_1_0 members=1 eligible=1 retained=0
    // REPORT: obelisk clocked kernel rejection: root accessed by event activation = 1
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk0: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %a: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %c: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk0 to ^body : !bitref
    ^body:
      %av = simulation.ref.load %a : !byteref -> !byte
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      %next = simulation.logic.binary add %av, %one : !byte
      simulation.nba.enqueue %next to %a : (!byte, !byteref) -> ()
      simulation.nba.enqueue %next to %c : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @cross(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk1: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %a: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %b: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk1 to ^body : !bitref
    ^body:
      %av = simulation.ref.load %a : !byteref -> !byte
      simulation.nba.enqueue %av to %b : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %b = simulation.context.storage %ctx[3] : !byteref
      %c = simulation.context.storage %ctx[4] : !byteref
      %bv = simulation.ref.load %b : !byteref -> !byte
      %cv = simulation.ref.load %c : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d %d"
      simulation.display %ctx to %stdout(%format, %bv, %cv) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !byte, !byte
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// `late` waits on `inner`, a copy of `port`, itself a copy of `clk` (23.3.3).
// It runs after both copies and always reads the new `port`. In a kernel that
// waits on `clk`, the copy into `port` may not have run yet (4.7), so a
// process that reads any root of its clock class stays out of the kernel.
// `count` alone still forms one, and its NBA becomes a store.

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {native.debug_timing} {
  simulation.design @alias_reader {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !byte design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 port_input hierarchy "copy"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 5 in 0 always_ff hierarchy "late"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.code_unit.decl 7 in 0 port_input hierarchy "copy_inner"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %port = simulation.context.storage %ctx[1] : !bitref
      %a = simulation.context.storage %ctx[2] : !byteref
      %seen = simulation.context.storage %ctx[3] : !bitref
      %inner = simulation.context.storage %ctx[4] : !bitref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @copy(%ctx, %clk, %port) : !simulation.context, !bitref, !bitref -> !simulation.process
      %p5 = simulation.spawn @copy_inner(%ctx, %port, %inner) : !simulation.context, !bitref, !bitref -> !simulation.process
      %p2 = simulation.spawn @count(%ctx, %clk, %a) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p3 = simulation.spawn @late(%ctx, %inner, %port, %seen) : !simulation.context, !bitref, !bitref, !bitref -> !simulation.process
      %p4 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !bitref) -> ()
      cf.br ^low
    }
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %port: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %clk : !bitref -> !bit
      simulation.ref.store %value to %port {simulation.continuous_store} : !bit, !bitref
      simulation.suspend.change %clk to ^body : !bitref
    }
    simulation.func @copy_inner(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %port: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %inner: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 9 : i32, code_unit_id = 7 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %port : !bitref -> !bit
      simulation.ref.store %value to %inner {simulation.continuous_store} : !bit, !bitref
      simulation.suspend.change %port to ^body : !bitref
    }
    // CHECK-LABEL: simulation.design @alias_reader
    // CHECK: simulation.spawn @__obelisk_clocked_0_1_0
    // CHECK: simulation.spawn @late
    // CHECK-NOT: simulation.func private @count
    // CHECK: simulation.func private @late
    // CHECK: simulation.nba.enqueue
    // CHECK: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: simulation.ref.store
    // REPORT: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=1
    // REPORT-DAG: obelisk clocked kernel rejection: member accesses its clock = 1
    // REPORT-DAG: obelisk clocked kernel rejection: member accesses its clock: read of another alias = 1
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %a: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %av = simulation.ref.load %a : !byteref -> !byte
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      %next = simulation.logic.binary add %av, %one : !byte
      simulation.nba.enqueue %next to %a : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func private @late(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %inner: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}, %port: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %inner to ^body : !bitref
    ^body:
      %pv = simulation.ref.load %port : !bitref -> !bit
      simulation.nba.enqueue %pv to %seen : (!bit, !bitref) -> ()
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %a = simulation.context.storage %ctx[2] : !byteref
      %seen = simulation.context.storage %ctx[3] : !bitref
      %av = simulation.ref.load %a : !byteref -> !byte
      %sv = simulation.ref.load %seen : !bitref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d %b"
      simulation.display %ctx to %stdout(%format, %av, %sv) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !byte, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// `q` has a blocking writer in the kernel, `set`, besides the NBA in
// `clear`. Their relative order in the slot decides the final `q` (4.9.4
// applies the NBA after the blocking store), so `q` keeps its NBA. `r` still
// becomes a store.

!bit = !simulation.logic<1>
!bitref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @mixed_writers {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "clear"
    simulation.code_unit.decl 4 in 0 always hierarchy "set"
    simulation.code_unit.decl 5 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !bitref
      %r = simulation.context.storage %ctx[2] : !bitref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @clear(%ctx, %clk, %q, %r) : !simulation.context, !bitref, !bitref, !bitref -> !simulation.process
      %p2 = simulation.spawn @set(%ctx, %clk, %q) : !simulation.context, !bitref, !bitref -> !simulation.process
      %p3 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !bitref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @mixed_writers
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: %[[ZERO:.*]] = simulation.logic.constant false, false
    // CHECK: simulation.nba.enqueue %[[ZERO]] to %[[Q:arg[0-9]+]]
    // CHECK-NEXT: simulation.ref.store %[[ZERO]] to %[[R:arg[0-9]+]]
    // CHECK: simulation.ref.store %{{.*}} to %[[Q]]
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=2 eligible=1 retained=0
    // REPORT: obelisk clocked kernel rejection: root writers = 1
    simulation.func private @clear(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      simulation.nba.enqueue %zero to %q : (!bit, !bitref) -> ()
      simulation.nba.enqueue %zero to %r : (!bit, !bitref) -> ()
      cf.br ^wait
    }
    simulation.func private @set(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %one = simulation.logic.constant true, false : !bit
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      simulation.ref.store %one to %q : !bit, !bitref
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %q = simulation.context.storage %ctx[1] : !bitref
      %r = simulation.context.storage %ctx[2] : !bitref
      %qv = simulation.ref.load %q : !bitref -> !bit
      %rv = simulation.ref.load %r : !bitref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %qv, %rv) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// An NBA to a dynamically selected element stores at its statement as well
// (4.9.4 evaluates the index there). The later read of another element in
// the same activation goes through the snapshot of the whole array, with the
// element view rebuilt on the snapshot.

!byte = !simulation.logic<8>
!bit = !simulation.logic<1>
!mem = !simulation.unpacked_array<0 : 3 x !byte>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
!memref = !simulation.ref<!mem>
module attributes {native.debug_timing} {
  simulation.design @dynamic_element {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !mem design
    simulation.storage.decl 2 in 0 : i64 design
    simulation.storage.decl 3 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "write"
    simulation.code_unit.decl 4 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %mem = simulation.context.storage %ctx[1] : !memref
      %index = simulation.context.storage %ctx[2] : !simulation.ref<i64>
      %out = simulation.context.storage %ctx[3] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @write(%ctx, %clk, %mem, %index, %out) : !simulation.context, !bitref, !memref, !simulation.ref<i64>, !byteref -> !simulation.process
      %p2 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !bitref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @dynamic_element
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: simulation.suspend.edge posedge %{{.*}} to ^[[START:bb[0-9]+]]
    // CHECK: ^[[START]]:
    // CHECK-NEXT: %[[OLD:.*]] = simulation.ref.load %[[MEM:arg[0-9]+]] : !simulation.ref<!simulation.unpacked_array
    // CHECK-NEXT: %[[SNAP:.*]] = simulation.ref.alloc %[[OLD]]
    // CHECK: %[[ELEMENT:.*]] = simulation.ref.array_element %[[MEM]][%{{.*}}]
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %[[ELEMENT]]
    // CHECK: %[[VIEW:.*]] = simulation.ref.array_element %[[SNAP]][%{{.*}}]
    // CHECK-NEXT: simulation.ref.load %[[VIEW]]
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: cf.br
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=2 retained=0 snapshots=1
    simulation.func private @write(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %mem: !memref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %index: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %value = simulation.logic.constant 7 : i8, 0 : i8 : !byte
      %first = arith.constant 0 : i64
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %i = simulation.ref.load %index : !simulation.ref<i64> -> i64
      %element = simulation.ref.array_element %mem[%i] : (!memref, i64) -> !byteref
      simulation.nba.enqueue %value to %element : (!byte, !byteref) -> ()
      %head = simulation.ref.array_element %mem[%first] : (!memref, i64) -> !byteref
      %hv = simulation.ref.load %head : !byteref -> !byte
      simulation.nba.enqueue %hv to %out : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %mem = simulation.context.storage %ctx[1] : !memref
      %out = simulation.context.storage %ctx[3] : !byteref
      %first = arith.constant 1 : i64
      %element = simulation.ref.array_element %mem[%first] : (!memref, i64) -> !byteref
      %ev = simulation.ref.load %element : !byteref -> !byte
      %ov = simulation.ref.load %out : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d %d"
      simulation.display %ctx to %stdout(%format, %ev, %ov) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !byte, !byte
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// A function called after the NBA reads `q` through the design context. The
// call needs the old `q` (4.9.4) but cannot be rebased onto a snapshot, so the
// kernel delays the update of `q` to its end.

!bit = !simulation.logic<1>
!bitref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @opaque_late_read {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 function hierarchy "peek"
    simulation.code_unit.decl 5 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !bitref
      %r = simulation.context.storage %ctx[2] : !bitref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q, %r) : !simulation.context, !bitref, !bitref, !bitref -> !simulation.process
      %p2 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !bitref) -> ()
      cf.br ^low
    }
    simulation.func @peek(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !bit attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %q = simulation.context.storage %ctx[1] : !bitref
      %qv = simulation.ref.load %q : !bitref -> !bit
      simulation.return %qv : !bit
    }
    // CHECK-LABEL: simulation.design @opaque_late_read
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: %[[NEXT:.*]] = simulation.logic.unary logical_not
    // CHECK-NEXT: simulation.ref.store %[[NEXT]] to %[[VALUE:[0-9]+]]
    // CHECK-NEXT: %[[SET:.*]] = arith.constant true
    // CHECK-NEXT: simulation.ref.store %[[SET]] to %[[FLAG:[0-9]+]]
    // CHECK-NEXT: %[[OLD:.*]] = simulation.call @peek
    // CHECK-NEXT: simulation.ref.store %[[OLD]] to %{{.*}}
    // CHECK: %[[IS:.*]] = simulation.ref.load %[[FLAG]]
    // CHECK-NEXT: cf.cond_br %[[IS]]
    // CHECK: %[[UPDATE:.*]] = simulation.ref.load %[[VALUE]]
    // CHECK-NEXT: simulation.ref.store %[[UPDATE]] to %arg2
    // CHECK-NOT: simulation.nba.enqueue
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=2 retained=0 snapshots=0 deferred=0 delayed=1
    simulation.func private @toggle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %old = simulation.ref.load %q : !bitref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !bitref) -> ()
      %seen = simulation.call @peek(%ctx) : (!simulation.context) -> !bit
      simulation.nba.enqueue %seen to %r : (!bit, !bitref) -> ()
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 100
      simulation.suspend.delay %delay to ^done
    ^done:
      %q = simulation.context.storage %ctx[1] : !bitref
      %r = simulation.context.storage %ctx[2] : !bitref
      %qv = simulation.ref.load %q : !bitref -> !bit
      %rv = simulation.ref.load %r : !bitref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %qv, %rv) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// `start` sets the clock at time zero and `clock` toggles it after each
// positive delay. Each writer runs at most once per slot, and only one of
// them can run in a given slot, so the kernel runs at most once per phase.

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {native.debug_timing} {
  simulation.design @slot_writers {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "start"
    simulation.code_unit.decl 3 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 5 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !byteref
      %p0 = simulation.spawn @start(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p2 = simulation.spawn @count(%ctx, %clk, %q) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p3 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @start(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = simulation.logic.constant false, false : !bit
      simulation.ref.store %zero to %clk : !bit, !bitref
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.ref.store %one to %clk : !bit, !bitref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !bitref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @slot_writers
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: simulation.ref.store %{{.*}} to %{{.*}} : !simulation.logic<8>
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=1 retained=0
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 5 : i64} {
      %q = simulation.context.storage %ctx[1] : !byteref
      %qv = simulation.ref.load %q : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d"
      simulation.display %ctx to %stdout(%format, %qv) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !byte
      simulation.return
    }
  }
}

// -----

// Cost: `q` keeps its NBA because a same-edge sampler reads it, and `s`
// depends on both `q` and `r`. Storing `r` directly would settle `s` once
// after the kernel and again after the NBA region, so `r` keeps its NBA too.
// `t` wakes no combinational logic and is stored directly.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @second_wave {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.storage.decl 5 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "stages"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "sum"
    simulation.code_unit.decl 5 in 0 initial hierarchy "sampler"
    simulation.code_unit.decl 6 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %t = simulation.context.storage %ctx[3] : !ref
      %s = simulation.context.storage %ctx[4] : !ref
      %seen = simulation.context.storage %ctx[5] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @stages(%ctx, %clk, %q, %r, %t) : !simulation.context, !ref, !ref, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @sum(%ctx, %q, %r, %s) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @sampler(%ctx, %clk, %q, %seen) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !ref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !ref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @second_wave
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: simulation.nba.enqueue %{{.*}} to %[[Q:arg[0-9]+]]
    // CHECK-NEXT: simulation.nba.enqueue %{{.*}} to %[[R:arg[0-9]+]]
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %[[T:arg[0-9]+]]
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=2 retained=1
    // REPORT-DAG: obelisk clocked kernel rejection: root accessed by event activation = 1
    // REPORT-DAG: obelisk clocked kernel rejection: root second wave = 1
    simulation.func private @stages(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %t: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %qv = simulation.ref.load %q : !ref -> !bit
      %rv = simulation.ref.load %r : !ref -> !bit
      %next = simulation.logic.unary logical_not %qv : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      simulation.nba.enqueue %qv to %r : (!bit, !ref) -> ()
      simulation.nba.enqueue %rv to %t : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @sum(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %s: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %qv = simulation.ref.load %q : !ref -> !bit
      %rv = simulation.ref.load %r : !ref -> !bit
      %sv = simulation.logic.binary xor %qv, %rv : !bit
      simulation.ref.store %sv to %s : !bit, !ref
      simulation.suspend.any %q, %r edges [0, 0] to ^body : !ref, !ref
    }
    simulation.func @sampler(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 6 : i64} {
      %t = simulation.context.storage %ctx[3] : !ref
      %s = simulation.context.storage %ctx[4] : !ref
      %seen = simulation.context.storage %ctx[5] : !ref
      %tv = simulation.ref.load %t : !ref -> !bit
      %sv = simulation.ref.load %s : !ref -> !bit
      %ev = simulation.ref.load %seen : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b %b"
      simulation.display %ctx to %stdout(%format, %tv, %sv, %ev) newline = true radix = <binary> flags = [0, 0, 0, 0] : !simulation.bytes, !bit, !bit, !bit
      simulation.return
    }
  }
}

// -----

// `q` keeps its NBA (sampled at the edge) but only `inv` reads it; `r` feeds
// `mix`, which no retained root of the kernel reaches, so `r` is stored
// directly. `mix` also reads `din`, which the testbench updates by NBA at
// every edge: that wave exists with or without the kernel and does not
// decide its conversion.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @disjoint_waves {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.storage.decl 5 in 0 : !bit design
    simulation.storage.decl 6 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "stages"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "inv"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "mix"
    simulation.code_unit.decl 6 in 0 initial hierarchy "sampler"
    simulation.code_unit.decl 7 in 0 initial hierarchy "stimulus"
    simulation.code_unit.decl 8 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %a = simulation.context.storage %ctx[3] : !ref
      %b = simulation.context.storage %ctx[4] : !ref
      %seen = simulation.context.storage %ctx[5] : !ref
      %din = simulation.context.storage %ctx[6] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @stages(%ctx, %clk, %q, %r, %din) : !simulation.context, !ref, !ref, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @inv(%ctx, %q, %a) : !simulation.context, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @mix(%ctx, %r, %din, %b) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @sampler(%ctx, %clk, %q, %seen) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p5 = simulation.spawn @stimulus(%ctx, %clk, %din) : !simulation.context, !ref, !ref -> !simulation.process
      %p6 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !ref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !ref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @disjoint_waves
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: simulation.nba.enqueue %{{.*}} to %[[Q:arg[0-9]+]]
    // CHECK-NEXT: simulation.ref.store %{{.*}} to %[[R:arg[0-9]+]]
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=1 retained=0
    // REPORT: obelisk clocked kernel rejection: root accessed by event activation = 1
    simulation.func private @stages(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %din: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %qv = simulation.ref.load %q : !ref -> !bit
      %dv = simulation.ref.load %din : !ref -> !bit
      %next = simulation.logic.unary logical_not %qv : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      simulation.nba.enqueue %dv to %r : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @inv(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %a: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %qv = simulation.ref.load %q : !ref -> !bit
      %av = simulation.logic.unary logical_not %qv : (!bit) -> !bit
      simulation.ref.store %av to %a : !bit, !ref
      simulation.suspend.change %q to ^body : !ref
    }
    simulation.func @mix(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %din: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %b: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %rv = simulation.ref.load %r : !ref -> !bit
      %dv = simulation.ref.load %din : !ref -> !bit
      %bv = simulation.logic.binary xor %rv, %dv : !bit
      simulation.ref.store %bv to %b : !bit, !ref
      simulation.suspend.any %r, %din edges [0, 0] to ^body : !ref, !ref
    }
    simulation.func @sampler(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @stimulus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %din: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %din : !ref -> !bit
      %next = simulation.logic.unary logical_not %v : (!bit) -> !bit
      simulation.nba.enqueue %next to %din : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 8 : i64} {
      %a = simulation.context.storage %ctx[3] : !ref
      %b = simulation.context.storage %ctx[4] : !ref
      %seen = simulation.context.storage %ctx[5] : !ref
      %av = simulation.ref.load %a : !ref -> !bit
      %bv = simulation.ref.load %b : !ref -> !bit
      %ev = simulation.ref.load %seen : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b %b"
      simulation.display %ctx to %stdout(%format, %av, %bv, %ev) newline = true radix = <binary> flags = [0, 0, 0, 0] : !simulation.bytes, !bit, !bit, !bit
      simulation.return
    }
  }
}

// -----

// IEEE 1800-2023 10.4.2: `late` executes `q <= #10 0` at time 3, before the
// kernel's NBA of time 13, so at time 13 the kernel's update is applied last
// and wins. Stored at its statement, the kernel's value would be overwritten
// by the delayed update, so `q` keeps its NBA even though `late` never shares
// the kernel's phase.

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {native.debug_timing} {
  simulation.design @delayed_writer {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 4 in 0 initial hierarchy "late"
    simulation.code_unit.decl 5 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @count(%ctx, %clk, %q) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p2 = simulation.spawn @late(%ctx, %q) : !simulation.context, !byteref -> !simulation.process
      %p3 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.nba.enqueue %one to %clk : (!bit, !bitref) -> ()
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.nba.enqueue %zero to %clk : (!bit, !bitref) -> ()
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @delayed_writer
    // CHECK-NOT: __obelisk_clocked
    // CHECK: simulation.func private @count
    // REPORT-NOT: obelisk clocked kernel:
    // REPORT: obelisk clocked kernel rejection: root writers = 1
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func @late(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %start = simulation.time.constant 3
      %after = simulation.time.constant 10
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !byte
      simulation.suspend.delay %start to ^write
    ^write:
      simulation.nba.enqueue %zero to %q after %after : (!byte, !byteref, !simulation.time) -> ()
      simulation.return
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 5 : i64} {
      %q = simulation.context.storage %ctx[1] : !byteref
      %qv = simulation.ref.load %q : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d"
      simulation.display %ctx to %stdout(%format, %qv) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !byte
      simulation.return
    }
  }
}

// -----

// IEEE 1800-2023 20.2 and 9.2.3: `stop` calls `$finish` in the kernel's
// phase, which ends the simulation at once, before the NBA region. The final
// procedure then reads `q` without its pending update, so `q` keeps its NBA:
// a direct store would be visible to it early.

!bit = !simulation.logic<1>
!byte = !simulation.logic<8>
!bitref = !simulation.ref<!bit>
!byteref = !simulation.ref<!byte>
module attributes {native.debug_timing} {
  simulation.design @finish_in_phase {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !byte design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "count"
    simulation.code_unit.decl 4 in 0 initial hierarchy "stop"
    simulation.code_unit.decl 5 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !bitref
      %q = simulation.context.storage %ctx[1] : !byteref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !bitref -> !simulation.process
      %p1 = simulation.spawn @count(%ctx, %clk, %q) : !simulation.context, !bitref, !byteref -> !simulation.process
      %p2 = simulation.spawn @stop(%ctx) : !simulation.context -> !simulation.process
      %p3 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 5
      %one = simulation.logic.constant true, false : !bit
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^low
    ^low:
      simulation.suspend.delay %delay to ^rise
    ^rise:
      simulation.ref.store %one to %clk : !bit, !bitref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !bitref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @finish_in_phase
    // CHECK-NOT: __obelisk_clocked
    // CHECK: simulation.func private @count
    // CHECK: simulation.nba.enqueue
    // REPORT-NOT: obelisk clocked kernel:
    // REPORT: obelisk clocked kernel rejection: root accessed by event activation = 1
    simulation.func private @count(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !bitref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !byteref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !bitref
    ^body:
      %old = simulation.ref.load %q : !byteref -> !byte
      %next = simulation.logic.binary add %old, %one : !byte
      simulation.nba.enqueue %next to %q : (!byte, !byteref) -> ()
      cf.br ^wait
    }
    simulation.func @stop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 25
      simulation.suspend.delay %delay to ^done
    ^done:
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 5 : i64} {
      %q = simulation.context.storage %ctx[1] : !byteref
      %qv = simulation.ref.load %q : !byteref -> !byte
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%d"
      simulation.display %ctx to %stdout(%format, %qv) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !byte
      simulation.return
    }
  }
}
