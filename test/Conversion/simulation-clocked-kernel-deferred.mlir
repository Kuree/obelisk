// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-clocked-kernels{defer=true}))' -o %t.mlir 2> %t.log
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=REPORT < %t.log

// IEEE 1800-2023 4.4.2.3, 4.5 and 4.7: a deferred kernel's wait resumes it
// in the Inactive region, as `@(posedge clk) #0` would: after every Active
// event of its pass. The original may run its
// members last in that pass, so this is equivalent when no other Inactive
// event shares the phase. Activations that ran first, such as a same-edge
// sampler or a testbench delay of that slot, read values before the kernel's
// stores. Only what the kernel's own updates wake runs after it.

// A blocking clock wakes the sampler and the kernel in the same phase as
// every testbench delay. Deferred, both roots are stored directly.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @same_edge {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "follow"
    simulation.code_unit.decl 5 in 0 initial hierarchy "sampler"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[3] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @follow(%ctx, %clk, %q, %r) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @sampler(%ctx, %clk, %q, %seen) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
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
      simulation.ref.store %one to %clk : !bit, !ref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !ref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @same_edge
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // Inactive is `#simulation.event_region` ordinal 3.
    // CHECK: simulation.suspend.edge posedge %{{.*}} to ^{{bb[0-9]+}} {resume_region = 3 : i32
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: simulation.ref.store
    // CHECK-NOT: simulation.nba.enqueue
    // CHECK: }
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=2 eligible=2 retained=0 snapshots=0 deferred=1
    simulation.func private @toggle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func private @follow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.nba.enqueue %v to %r : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @sampler(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 1000
      simulation.suspend.delay %delay to ^done
    ^done:
      %r = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[3] : !ref
      %rv = simulation.ref.load %r : !ref -> !bit
      %sv = simulation.ref.load %seen : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %rv, %sv) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// The sampler reads `q` after a `#0`. That Inactive event may run before or
// after the deferred kernel, so `q` keeps its NBA; it writes nothing the
// kernel reads, so deferral stays equivalent. The testbench delay reads `r`
// and calls `$finish` in the kernel's pass, but as an Active event it runs
// first, and no kernel runs after a finish. `r` and `t`, which a final
// procedure reads, are stored directly.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @inactive {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "follow"
    simulation.code_unit.decl 5 in 0 initial hierarchy "sampler"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.code_unit.decl 7 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[3] : !ref
      %t = simulation.context.storage %ctx[4] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @follow(%ctx, %clk, %q, %r, %t) : !simulation.context, !ref, !ref, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @sampler(%ctx, %clk, %q, %seen) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
      %p5 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
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
      simulation.ref.store %one to %clk : !bit, !ref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !ref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @inactive
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: resume_region = 3 : i32
    // CHECK: %[[V:.*]] = simulation.ref.load
    // CHECK-NEXT: simulation.ref.store %[[V]] to
    // CHECK-NEXT: simulation.ref.store %[[V]] to
    // CHECK: simulation.nba.enqueue
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=2 eligible=2 retained=0 snapshots=0 deferred=1
    // REPORT-NEXT: obelisk clocked kernel rejection: root accessed by event activation = 1
    simulation.func private @toggle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func private @follow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %t: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.nba.enqueue %v to %r : (!bit, !ref) -> ()
      simulation.nba.enqueue %v to %t : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @sampler(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %none = simulation.time.constant 0
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^yield : !ref
    ^yield:
      simulation.suspend.delay %none to ^body
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 1000
      simulation.suspend.delay %delay to ^done
    ^done:
      %r = simulation.context.storage %ctx[2] : !ref
      %seen = simulation.context.storage %ctx[3] : !ref
      %rv = simulation.ref.load %r : !ref -> !bit
      %sv = simulation.ref.load %seen : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %rv, %sv) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 7 : i64} {
      %t = simulation.context.storage %ctx[4] : !ref
      %tv = simulation.ref.load %t : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %tv) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      simulation.return
    }
  }
}

// -----

// `strobe` flips `en` after a `#0`, an Inactive event of the kernel's phase.
// It may run before a deferred kernel, whose member `follow` would then read
// the new `en` where the original reads the old one, so deferral is refused.
// The immediate kernel keeps the NBA of `r`, which the testbench delay reads
// in the kernel's phase, and stores `q` directly.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @inactive_input {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always_ff hierarchy "toggle"
    simulation.code_unit.decl 4 in 0 always_ff hierarchy "follow"
    simulation.code_unit.decl 5 in 0 initial hierarchy "strobe"
    simulation.code_unit.decl 6 in 0 initial hierarchy "report"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %r = simulation.context.storage %ctx[2] : !ref
      %en = simulation.context.storage %ctx[3] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @toggle(%ctx, %clk, %q) : !simulation.context, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @follow(%ctx, %clk, %q, %en, %r) : !simulation.context, !ref, !ref, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @strobe(%ctx, %clk, %en) : !simulation.context, !ref, !ref -> !simulation.process
      %p4 = simulation.spawn @report(%ctx) : !simulation.context -> !simulation.process
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
      simulation.ref.store %one to %clk : !bit, !ref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !ref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @inactive_input
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK-NOT: resume_region
    // CHECK: simulation.nba.enqueue %{{.*}} to %[[R:arg[0-9]+]]
    // CHECK: simulation.ref.store
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=2 eligible=1 retained=0 snapshots=0 deferred=0
    // REPORT-DAG: obelisk clocked kernel rejection: deferral: inactive event writes kernel input = 1
    // REPORT-DAG: obelisk clocked kernel rejection: root accessed by event activation = 1
    simulation.func private @toggle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func private @follow(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %en: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 5 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %e = simulation.ref.load %en : !ref -> !bit
      %v = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.binary and %v, %e : !bit
      simulation.nba.enqueue %next to %r : (!bit, !ref) -> ()
      cf.br ^wait
    }
    simulation.func @strobe(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %en: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %none = simulation.time.constant 0
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^yield : !ref
    ^yield:
      simulation.suspend.delay %none to ^body
    ^body:
      %old = simulation.ref.load %en : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.ref.store %next to %en : !bit, !ref
      cf.br ^wait
    }
    simulation.func @report(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = simulation.time.constant 1000
      simulation.suspend.delay %delay to ^done
    ^done:
      %r = simulation.context.storage %ctx[2] : !ref
      %rv = simulation.ref.load %r : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %rv) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}

// -----

// `pulse` flips `tick` with a blocking store, and `relay` wakes on it after
// the kernel and lowers the clock: the kernel could run twice in one phase.
// Deferral is refused, and the clock has two writers for an immediate kernel.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @clock_after {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "pulse"
    simulation.code_unit.decl 4 in 0 initial hierarchy "relay"
    simulation.code_unit.decl 5 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %tick = simulation.context.storage %ctx[2] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @pulse(%ctx, %clk, %q, %tick) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @relay(%ctx, %clk, %tick) : !simulation.context, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
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
      simulation.ref.store %one to %clk : !bit, !ref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !ref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @clock_after
    // CHECK-NOT: __obelisk_clocked
    // CHECK: simulation.func private @pulse
    // REPORT-NOT: obelisk clocked kernel:
    // REPORT-DAG: obelisk clocked kernel rejection: clock source = 1
    // REPORT-DAG: obelisk clocked kernel rejection: deferral: clock changes after kernel = 1
    simulation.func private @pulse(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %tick: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      %t = simulation.ref.load %tick : !ref -> !bit
      %flip = simulation.logic.unary logical_not %t : (!bit) -> !bit
      simulation.ref.store %flip to %tick : !bit, !ref
      cf.br ^wait
    }
    simulation.func @relay(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %tick: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %zero = simulation.logic.constant false, false : !bit
      cf.br ^wait
    ^wait:
      simulation.suspend.change %tick to ^body : !ref
    ^body:
      simulation.ref.store %zero to %clk : !bit, !ref
      cf.br ^wait
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 5 : i64} {
      %q = simulation.context.storage %ctx[1] : !ref
      %qv = simulation.ref.load %q : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b"
      simulation.display %ctx to %stdout(%format, %qv) newline = true radix = <binary> flags = [0, 0] : !simulation.bytes, !bit
      simulation.return
    }
  }
}

// -----

// `pulse` also flips `tick`, which wakes `watch` after the kernel: `watch`
// must read the old `q` (4.9.4), so `q` keeps its NBA. The same-edge sampler
// of `r` runs before the deferred kernel, so `r` is stored directly.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
module attributes {native.debug_timing} {
  simulation.design @after_reader {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bit design
    simulation.storage.decl 1 in 0 : !bit design
    simulation.storage.decl 2 in 0 : !bit design
    simulation.storage.decl 3 in 0 : !bit design
    simulation.storage.decl 4 in 0 : !bit design
    simulation.storage.decl 5 in 0 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "pulse"
    simulation.code_unit.decl 4 in 0 initial hierarchy "watch"
    simulation.code_unit.decl 5 in 0 initial hierarchy "sampler"
    simulation.code_unit.decl 6 in 0 final hierarchy "last"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !ref
      %q = simulation.context.storage %ctx[1] : !ref
      %tick = simulation.context.storage %ctx[2] : !ref
      %r = simulation.context.storage %ctx[3] : !ref
      %seen = simulation.context.storage %ctx[4] : !ref
      %other = simulation.context.storage %ctx[5] : !ref
      %p0 = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !ref -> !simulation.process
      %p1 = simulation.spawn @pulse(%ctx, %clk, %q, %tick, %r) : !simulation.context, !ref, !ref, !ref, !ref -> !simulation.process
      %p2 = simulation.spawn @watch(%ctx, %tick, %q, %seen) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %p3 = simulation.spawn @sampler(%ctx, %clk, %r, %other) : !simulation.context, !ref, !ref, !ref -> !simulation.process
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
      simulation.ref.store %one to %clk : !bit, !ref
      simulation.suspend.delay %delay to ^fall
    ^fall:
      simulation.ref.store %zero to %clk : !bit, !ref
      cf.br ^low
    }
    // CHECK-LABEL: simulation.design @after_reader
    // CHECK-LABEL: simulation.func private @__obelisk_clocked_0_1_0
    // CHECK: resume_region = 3 : i32
    // CHECK: simulation.nba.enqueue %{{.*}} to %[[Q:arg[0-9]+]]
    // CHECK: simulation.ref.store %{{.*}} to %[[R:arg[0-9]+]]
    // REPORT-LABEL: obelisk clocked kernel: __obelisk_clocked_0_1_0 members=1 eligible=1 retained=0 snapshots=0 deferred=1
    // REPORT: obelisk clocked kernel rejection: root accessed by event activation = 1
    simulation.func private @pulse(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %tick: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %old = simulation.ref.load %q : !ref -> !bit
      %next = simulation.logic.unary logical_not %old : (!bit) -> !bit
      simulation.nba.enqueue %next to %q : (!bit, !ref) -> ()
      simulation.nba.enqueue %old to %r : (!bit, !ref) -> ()
      %t = simulation.ref.load %tick : !ref -> !bit
      %flip = simulation.logic.unary logical_not %t : (!bit) -> !bit
      simulation.ref.store %flip to %tick : !bit, !ref
      cf.br ^wait
    }
    simulation.func @watch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %tick: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %q: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %seen: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %tick to ^body : !ref
    ^body:
      %v = simulation.ref.load %q : !ref -> !bit
      simulation.ref.store %v to %seen : !bit, !ref
      cf.br ^wait
    }
    simulation.func @sampler(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %r: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %other: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !ref
    ^body:
      %v = simulation.ref.load %r : !ref -> !bit
      simulation.ref.store %v to %other : !bit, !ref
      cf.br ^wait
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 2 : i32, code_unit_id = 6 : i64} {
      %seen = simulation.context.storage %ctx[4] : !ref
      %other = simulation.context.storage %ctx[5] : !ref
      %sv = simulation.ref.load %seen : !ref -> !bit
      %ov = simulation.ref.load %other : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = simulation.bytes.constant "%b %b"
      simulation.display %ctx to %stdout(%format, %sv, %ov) newline = true radix = <binary> flags = [0, 0, 0] : !simulation.bytes, !bit, !bit
      simulation.return
    }
  }
}
