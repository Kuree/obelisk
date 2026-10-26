// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements,obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{max-members=3},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=CAP
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{grow-acyclic=false},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=NOGROW

// Every design has an `initial` sink that reads the roots only written by
// the processes under test; a write-only root would lose its stores and then
// its writer process before the cohort is planned.
// IEEE 1800-2023 4.3, 4.7: a cohort runs the acyclic combinational owners
// connected to it, or woken by the same edge-triggered procedure, in data
// order. Owners whose intermediate values an edge waiter could see, or that
// wake on a clock, do not join by growth; admission decides any cohort the
// closure still extends with one.

// The cyclic cohort absorbs the acyclic owners on both sides of it, and
// @woken, which shares no storage with it but is woken by the same @flop.
// @island is never woken by an edge-triggered procedure and stays out.
// CHECK-LABEL: simulation.design @grow
// CHECK-DAG: simulation.spawn @flop
// CHECK-DAG: simulation.spawn @island
// CHECK-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// Only @flop's outputs wake the kernel. It runs @before, the cyclic pair and
// @after in data order, then @woken.
// CHECK-LABEL: simulation.func private @__obelisk_region_kernel_0_0
// CHECK: %[[IN4:.*]] = simulation.context.storage %arg0[4]
// CHECK: %[[IN6:.*]] = simulation.context.storage %arg0[6]
// CHECK: ^bb1:
// CHECK: %[[S3:.*]] = simulation.context.storage %arg0[3]
// CHECK: %[[S7:.*]] = simulation.context.storage %arg0[7]
// CHECK: %[[S0:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[V:.*]] = simulation.ref.load %[[IN4]]
// CHECK: simulation.ref.store %[[V]] to %[[S0]]
// CHECK: %[[S1:.*]] = simulation.context.storage %arg0[1]
// CHECK: %[[S2:.*]] = simulation.context.storage %arg0[2]
// CHECK: simulation.ref.store %[[V]] to %[[S1]]
// CHECK: %[[T:.*]] = arith.addi %[[V]],
// CHECK: simulation.ref.store %[[T]] to %[[S2]]
// CHECK: simulation.ref.store %[[T]] to %[[S3]]
// CHECK: simulation.ref.store %[[T]] to %[[S7]]
// CHECK: %[[S8:.*]] = simulation.context.storage %arg0[8]
// CHECK: %[[W:.*]] = simulation.ref.load %[[IN6]]
// CHECK: simulation.ref.store %[[W]] to %[[S8]]
// CHECK: simulation.suspend.any %[[IN4]], %[[IN6]] edges [0, 0] to ^bb1
// With room for only three members the grown cohort is rejected; the cyclic
// cohort it contains is still planned.
// CAP-LABEL: simulation.design @grow
// CAP-DAG: simulation.spawn @before
// CAP-DAG: simulation.spawn @after
// CAP-DAG: simulation.spawn @woken
// CAP-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// CAP: simulation.return
// Without growth only the cyclic pair forms a cohort.
// NOGROW-LABEL: simulation.design @grow
// NOGROW-DAG: simulation.spawn @flop
// NOGROW-DAG: simulation.spawn @before
// NOGROW-DAG: simulation.spawn @after
// NOGROW-DAG: simulation.spawn @woken
// NOGROW-DAG: simulation.spawn @island
// NOGROW-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// NOGROW-NOT: simulation.spawn
// NOGROW: simulation.return
!c = !simulation.context
!r = !simulation.ref<i8>
!b = !simulation.ref<i1>
module {
  simulation.design @grow {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i1 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "flop"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "before"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "after"
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "woken"
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "island"
    simulation.code_unit.decl 8 in 0 initial hierarchy "sink"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %sink = simulation.spawn @sink(%ctx) : !c -> !simulation.process
      %0 = simulation.spawn @producer_and_consumer(%ctx) : !c -> !simulation.process
      %1 = simulation.spawn @transform(%ctx) : !c -> !simulation.process
      %2 = simulation.spawn @flop(%ctx) : !c -> !simulation.process
      %3 = simulation.spawn @before(%ctx) : !c -> !simulation.process
      %4 = simulation.spawn @after(%ctx) : !c -> !simulation.process
      %5 = simulation.spawn @woken(%ctx) : !c -> !simulation.process
      %6 = simulation.spawn @island(%ctx) : !c -> !simulation.process
      simulation.return
    }
    simulation.func @sink(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %s7 = simulation.context.storage %ctx[7] : !r
      %l7 = simulation.ref.load %s7 : !r -> i8
      %s8 = simulation.context.storage %ctx[8] : !r
      %l8 = simulation.ref.load %s8 : !r -> i8
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !r
      %bank_address = simulation.context.storage %ctx[1] : !r
      %bank_result = simulation.context.storage %ctx[2] : !r
      %result = simulation.context.storage %ctx[3] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !r -> i8
      simulation.ref.store %a to %bank_address : i8, !r
      %b = simulation.ref.load %bank_result : !r -> i8
      simulation.ref.store %b to %result : i8, !r
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !r, !r
    }
    simulation.func @transform(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !r
      %result = simulation.context.storage %ctx[2] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !r -> i8
      %one = arith.constant 1 : i8
      %b = arith.addi %a, %one : i8
      simulation.ref.store %b to %result : i8, !r
      simulation.suspend.change %address to ^body : !r
    }
    simulation.func @flop(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %clk = simulation.context.storage %ctx[5] : !b
      %d = simulation.context.storage %ctx[10] : !r
      %q4 = simulation.context.storage %ctx[4] : !r
      %q6 = simulation.context.storage %ctx[6] : !r
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !b
    ^body:
      %v = simulation.ref.load %d : !r -> i8
      simulation.ref.store %v to %q4 : i8, !r
      simulation.ref.store %v to %q6 : i8, !r
      cf.br ^wait
    }
    simulation.func @before(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %in4 = simulation.context.storage %ctx[4] : !r
      %out = simulation.context.storage %ctx[0] : !r
      cf.br ^body
    ^body:
      %v4 = simulation.ref.load %in4 : !r -> i8
      simulation.ref.store %v4 to %out : i8, !r
      simulation.suspend.change %in4 to ^body : !r
    }
    simulation.func @after(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %in3 = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[7] : !r
      cf.br ^body
    ^body:
      %v3 = simulation.ref.load %in3 : !r -> i8
      simulation.ref.store %v3 to %out : i8, !r
      simulation.suspend.change %in3 to ^body : !r
    }
    simulation.func @woken(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %in6 = simulation.context.storage %ctx[6] : !r
      %out = simulation.context.storage %ctx[8] : !r
      cf.br ^body
    ^body:
      %v6 = simulation.ref.load %in6 : !r -> i8
      simulation.ref.store %v6 to %out : i8, !r
      simulation.suspend.change %in6 to ^body : !r
    }
    simulation.func @island(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %in9 = simulation.context.storage %ctx[9] : !r
      %out = simulation.context.storage %ctx[10] : !r
      cf.br ^body
    ^body:
      %v9 = simulation.ref.load %in9 : !r -> i8
      simulation.ref.store %v9 to %out : i8, !r
      simulation.suspend.change %in9 to ^body : !r
    }
  }
}

// -----

// @clocked wakes on a posedge-watched clock and @observed publishes to an
// edge waiter, so both stay out, and so does @tail below @clocked. @gate
// wakes on the clock and feeds the cohort; no owner above it may join, since
// one fed by the cohort would close the cycle cohort -> @gate -> cohort, so
// @pre stays out too. @after joins.
// CHECK-LABEL: simulation.design @blocked
// CAP-LABEL: simulation.design @blocked
// CHECK-DAG: simulation.spawn @flop
// CHECK-DAG: simulation.spawn @clocked
// CHECK-DAG: simulation.spawn @tail
// CHECK-DAG: simulation.spawn @observed
// CHECK-DAG: simulation.spawn @watcher
// CHECK-DAG: simulation.spawn @pre
// CHECK-DAG: simulation.spawn @gate
// CHECK-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
!c = !simulation.context
!r = !simulation.ref<i8>
!b = !simulation.ref<i1>
module {
  simulation.design @blocked {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i1 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.storage.decl 11 in 0 : i8 design
    simulation.storage.decl 12 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "flop"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "clocked"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "tail"
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "observed"
    simulation.code_unit.decl 7 in 0 always hierarchy "watcher"
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "pre"
    simulation.code_unit.decl 9 in 0 always_comb hierarchy "gate"
    simulation.code_unit.decl 10 in 0 always_comb hierarchy "after"
    simulation.code_unit.decl 11 in 0 initial hierarchy "sink"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %sink = simulation.spawn @sink(%ctx) : !c -> !simulation.process
      %0 = simulation.spawn @producer_and_consumer(%ctx) : !c -> !simulation.process
      %1 = simulation.spawn @transform(%ctx) : !c -> !simulation.process
      %2 = simulation.spawn @flop(%ctx) : !c -> !simulation.process
      %3 = simulation.spawn @clocked(%ctx) : !c -> !simulation.process
      %4 = simulation.spawn @tail(%ctx) : !c -> !simulation.process
      %5 = simulation.spawn @observed(%ctx) : !c -> !simulation.process
      %6 = simulation.spawn @watcher(%ctx) : !c -> !simulation.process
      %7 = simulation.spawn @pre(%ctx) : !c -> !simulation.process
      %8 = simulation.spawn @gate(%ctx) : !c -> !simulation.process
      %9 = simulation.spawn @after(%ctx) : !c -> !simulation.process
      simulation.return
    }
    simulation.func @sink(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 11 : i64} {
      %s11 = simulation.context.storage %ctx[11] : !r
      %l11 = simulation.ref.load %s11 : !r -> i8
      %s12 = simulation.context.storage %ctx[12] : !r
      %l12 = simulation.ref.load %s12 : !r -> i8
      %s6 = simulation.context.storage %ctx[6] : !r
      %l6 = simulation.ref.load %s6 : !r -> i8
      %s8 = simulation.context.storage %ctx[8] : !r
      %l8 = simulation.ref.load %s8 : !r -> i8
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !r
      %bank_address = simulation.context.storage %ctx[1] : !r
      %bank_result = simulation.context.storage %ctx[2] : !r
      %result = simulation.context.storage %ctx[3] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !r -> i8
      simulation.ref.store %a to %bank_address : i8, !r
      %b = simulation.ref.load %bank_result : !r -> i8
      simulation.ref.store %b to %result : i8, !r
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !r, !r
    }
    simulation.func @transform(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !r
      %result = simulation.context.storage %ctx[2] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !r -> i8
      %one = arith.constant 1 : i8
      %b = arith.addi %a, %one : i8
      simulation.ref.store %b to %result : i8, !r
      simulation.suspend.change %address to ^body : !r
    }
    simulation.func @flop(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %clk = simulation.context.storage %ctx[5] : !b
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^tick : !b
    ^tick:
      %out11 = simulation.context.storage %ctx[11] : !r
      %one11 = arith.constant 1 : i8
      simulation.ref.store %one11 to %out11 : i8, !r
      cf.br ^wait
    }
    simulation.func @clocked(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %in3 = simulation.context.storage %ctx[3] : !r
      %clk = simulation.context.storage %ctx[5] : !b
      %out = simulation.context.storage %ctx[4] : !r
      cf.br ^body
    ^body:
      %v3 = simulation.ref.load %in3 : !r -> i8
      %k = simulation.ref.load %clk : !b -> i1
      %z = arith.constant 0 : i8
      %m = arith.select %k, %v3, %z : i8
      simulation.ref.store %m to %out : i8, !r
      simulation.suspend.any %in3, %clk edges [0, 0] to ^body : !r, !b
    }
    simulation.func @tail(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %in4 = simulation.context.storage %ctx[4] : !r
      %out = simulation.context.storage %ctx[6] : !r
      cf.br ^body
    ^body:
      %v4 = simulation.ref.load %in4 : !r -> i8
      simulation.ref.store %v4 to %out : i8, !r
      simulation.suspend.change %in4 to ^body : !r
    }
    simulation.func @observed(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64} {
      %in3 = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[7] : !r
      cf.br ^body
    ^body:
      %v3 = simulation.ref.load %in3 : !r -> i8
      simulation.ref.store %v3 to %out : i8, !r
      simulation.suspend.change %in3 to ^body : !r
    }
    simulation.func @watcher(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      %clk = simulation.context.storage %ctx[7] : !r
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^tick : !r
    ^tick:
      %out12 = simulation.context.storage %ctx[12] : !r
      %one12 = arith.constant 1 : i8
      simulation.ref.store %one12 to %out12 : i8, !r
      cf.br ^wait
    }
    simulation.func @pre(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64} {
      %in9 = simulation.context.storage %ctx[9] : !r
      %out = simulation.context.storage %ctx[10] : !r
      cf.br ^body
    ^body:
      %v9 = simulation.ref.load %in9 : !r -> i8
      simulation.ref.store %v9 to %out : i8, !r
      simulation.suspend.change %in9 to ^body : !r
    }
    simulation.func @gate(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 9 : i64} {
      %in10 = simulation.context.storage %ctx[10] : !r
      %clk = simulation.context.storage %ctx[5] : !b
      %out = simulation.context.storage %ctx[0] : !r
      cf.br ^body
    ^body:
      %v10 = simulation.ref.load %in10 : !r -> i8
      %k = simulation.ref.load %clk : !b -> i1
      %z = arith.constant 0 : i8
      %m = arith.select %k, %v10, %z : i8
      simulation.ref.store %m to %out : i8, !r
      simulation.suspend.any %in10, %clk edges [0, 0] to ^body : !r, !b
    }
    simulation.func @after(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 10 : i64} {
      %in3 = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[8] : !r
      cf.br ^body
    ^body:
      %v3 = simulation.ref.load %in3 : !r -> i8
      simulation.ref.store %v3 to %out : i8, !r
      simulation.suspend.change %in3 to ^body : !r
    }
  }
}

// -----

// Without a cyclic cohort, the owners one edge-triggered procedure wakes still
// form one.
// CHECK-LABEL: simulation.design @seedless
// CHECK-DAG: simulation.spawn @flop
// CHECK-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
// NOGROW-LABEL: simulation.design @seedless
// NOGROW-DAG: simulation.spawn @flop
// NOGROW-DAG: simulation.spawn @first
// NOGROW-DAG: simulation.spawn @second
// NOGROW-NOT: simulation.spawn
// NOGROW: simulation.return
!c = !simulation.context
!r = !simulation.ref<i8>
!b = !simulation.ref<i1>
module {
  simulation.design @seedless {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always hierarchy "flop"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %0 = simulation.spawn @flop(%ctx) : !c -> !simulation.process
      %1 = simulation.spawn @first(%ctx) : !c -> !simulation.process
      %2 = simulation.spawn @second(%ctx) : !c -> !simulation.process
      simulation.return
    }
    simulation.func @flop(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[3] : !b
      %d = simulation.context.storage %ctx[2] : !r
      %q0 = simulation.context.storage %ctx[0] : !r
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !b
    ^body:
      %v = simulation.ref.load %d : !r -> i8
      simulation.ref.store %v to %q0 : i8, !r
      cf.br ^wait
    }
    simulation.func @first(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %in0 = simulation.context.storage %ctx[0] : !r
      %out = simulation.context.storage %ctx[1] : !r
      cf.br ^body
    ^body:
      %v0 = simulation.ref.load %in0 : !r -> i8
      simulation.ref.store %v0 to %out : i8, !r
      simulation.suspend.change %in0 to ^body : !r
    }
    simulation.func @second(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %in1 = simulation.context.storage %ctx[1] : !r
      %out = simulation.context.storage %ctx[2] : !r
      cf.br ^body
    ^body:
      %v1 = simulation.ref.load %in1 : !r -> i8
      simulation.ref.store %v1 to %out : i8, !r
      simulation.suspend.change %in1 to ^body : !r
    }
  }
}

// -----

// @bridge joins the islands of two cyclic cohorts, so the closure absorbs
// @observed, which lies between them although an edge waiter sees its writes.
// Each process's statement order still interleaves, and the observer
// certificate admits the whole cohort.
// CHECK-LABEL: simulation.design @two_cohorts
// CHECK-DAG: simulation.spawn @watcher
// CHECK-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.return
!c = !simulation.context
!r = !simulation.ref<i8>
module {
  simulation.design @two_cohorts {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "first_outer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first_inner"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second_outer"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "second_inner"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "observed"
    simulation.code_unit.decl 6 in 0 always hierarchy "watcher"
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "bridge"
    simulation.code_unit.decl 8 in 0 initial hierarchy "sink"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %sink = simulation.spawn @sink(%ctx) : !c -> !simulation.process
      %0 = simulation.spawn @first_outer(%ctx) : !c -> !simulation.process
      %1 = simulation.spawn @first_inner(%ctx) : !c -> !simulation.process
      %2 = simulation.spawn @second_outer(%ctx) : !c -> !simulation.process
      %3 = simulation.spawn @second_inner(%ctx) : !c -> !simulation.process
      %4 = simulation.spawn @observed(%ctx) : !c -> !simulation.process
      %5 = simulation.spawn @watcher(%ctx) : !c -> !simulation.process
      %6 = simulation.spawn @bridge(%ctx) : !c -> !simulation.process
      simulation.return
    }
    simulation.func @sink(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %s10 = simulation.context.storage %ctx[10] : !r
      %l10 = simulation.ref.load %s10 : !r -> i8
      %s8 = simulation.context.storage %ctx[8] : !r
      %l8 = simulation.ref.load %s8 : !r -> i8
      %s9 = simulation.context.storage %ctx[9] : !r
      %l9 = simulation.ref.load %s9 : !r -> i8
      simulation.return
    }
    // First cyclic cohort: 0 -> outer -> 1 -> inner -> 2 -> outer -> 3.
    simulation.func @first_outer(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !r
      %to = simulation.context.storage %ctx[1] : !r
      %back = simulation.context.storage %ctx[2] : !r
      %out = simulation.context.storage %ctx[3] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %to : i8, !r
      %b = simulation.ref.load %back : !r -> i8
      simulation.ref.store %b to %out : i8, !r
      simulation.suspend.any %in, %back edges [0, 0] to ^body : !r, !r
    }
    simulation.func @first_inner(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %in = simulation.context.storage %ctx[1] : !r
      %out = simulation.context.storage %ctx[2] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      simulation.suspend.change %in to ^body : !r
    }
    // Second cyclic cohort: 4 -> outer -> 5 -> inner -> 6 -> outer -> 7.
    simulation.func @second_outer(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %in = simulation.context.storage %ctx[4] : !r
      %to = simulation.context.storage %ctx[5] : !r
      %back = simulation.context.storage %ctx[6] : !r
      %out = simulation.context.storage %ctx[7] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %to : i8, !r
      %b = simulation.ref.load %back : !r -> i8
      simulation.ref.store %b to %out : i8, !r
      simulation.suspend.any %in, %back edges [0, 0] to ^body : !r, !r
    }
    simulation.func @second_inner(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %in = simulation.context.storage %ctx[5] : !r
      %out = simulation.context.storage %ctx[6] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      simulation.suspend.change %in to ^body : !r
    }
    // first -> observed -> second, and an edge waiter sees observed's output.
    simulation.func @observed(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %in = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[4] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      simulation.suspend.change %in to ^body : !r
    }
    simulation.func @watcher(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      %w = simulation.context.storage %ctx[4] : !r
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %w to ^tick : !r
    ^tick:
      %out10 = simulation.context.storage %ctx[10] : !r
      %one10 = arith.constant 1 : i8
      simulation.ref.store %one10 to %out10 : i8, !r
      cf.br ^wait
    }
    // @bridge reads both cohorts' outputs, which joins their islands.
    simulation.func @bridge(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %in = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[8] : !r
      %in2 = simulation.context.storage %ctx[7] : !r
      %out2 = simulation.context.storage %ctx[9] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      %b = simulation.ref.load %in2 : !r -> i8
      simulation.ref.store %b to %out2 : i8, !r
      simulation.suspend.any %in, %in2 edges [0, 0] to ^body : !r, !r
    }
  }
}

// -----

// Each outer process now reads its feedback first, so only a data-dependence
// order serves either cohort, and @watcher observes the transitions it would
// remove through @observed. Admission rejects the grown cohort, which falls
// back to the closed cohorts: the second one is admitted on its own.
// CHECK-LABEL: simulation.design @two_cohorts_reordered
// CHECK-DAG: simulation.spawn @first_outer
// CHECK-DAG: simulation.spawn @first_inner
// CHECK-DAG: simulation.spawn @observed
// CHECK-DAG: simulation.spawn @watcher
// CHECK-DAG: simulation.spawn @bridge
// CHECK-DAG: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK-LABEL: simulation.func private @__obelisk_region_kernel_0_0
// CHECK-DAG: simulation.context.storage %arg0[5]
// CHECK-DAG: simulation.context.storage %arg0[6]
// CHECK-NOT: simulation.context.storage %arg0[1]
// CHECK: simulation.suspend
!c = !simulation.context
!r = !simulation.ref<i8>
module {
  simulation.design @two_cohorts_reordered {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i8 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.storage.decl 6 in 0 : i8 design
    simulation.storage.decl 7 in 0 : i8 design
    simulation.storage.decl 8 in 0 : i8 design
    simulation.storage.decl 9 in 0 : i8 design
    simulation.storage.decl 10 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "first_outer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first_inner"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second_outer"
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "second_inner"
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "observed"
    simulation.code_unit.decl 6 in 0 always hierarchy "watcher"
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "bridge"
    simulation.code_unit.decl 8 in 0 initial hierarchy "sink"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %sink = simulation.spawn @sink(%ctx) : !c -> !simulation.process
      %0 = simulation.spawn @first_outer(%ctx) : !c -> !simulation.process
      %1 = simulation.spawn @first_inner(%ctx) : !c -> !simulation.process
      %2 = simulation.spawn @second_outer(%ctx) : !c -> !simulation.process
      %3 = simulation.spawn @second_inner(%ctx) : !c -> !simulation.process
      %4 = simulation.spawn @observed(%ctx) : !c -> !simulation.process
      %5 = simulation.spawn @watcher(%ctx) : !c -> !simulation.process
      %6 = simulation.spawn @bridge(%ctx) : !c -> !simulation.process
      simulation.return
    }
    simulation.func @sink(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %s10 = simulation.context.storage %ctx[10] : !r
      %l10 = simulation.ref.load %s10 : !r -> i8
      %s8 = simulation.context.storage %ctx[8] : !r
      %l8 = simulation.ref.load %s8 : !r -> i8
      %s9 = simulation.context.storage %ctx[9] : !r
      %l9 = simulation.ref.load %s9 : !r -> i8
      simulation.return
    }
    // First cyclic cohort: 0 -> outer -> 1 -> inner -> 2 -> outer -> 3.
    simulation.func @first_outer(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !r
      %to = simulation.context.storage %ctx[1] : !r
      %back = simulation.context.storage %ctx[2] : !r
      %out = simulation.context.storage %ctx[3] : !r
      cf.br ^body
    ^body:
      %b = simulation.ref.load %back : !r -> i8
      simulation.ref.store %b to %out : i8, !r
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %to : i8, !r
      simulation.suspend.any %in, %back edges [0, 0] to ^body : !r, !r
    }
    simulation.func @first_inner(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %in = simulation.context.storage %ctx[1] : !r
      %out = simulation.context.storage %ctx[2] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      simulation.suspend.change %in to ^body : !r
    }
    // Second cyclic cohort: 4 -> outer -> 5 -> inner -> 6 -> outer -> 7.
    simulation.func @second_outer(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %in = simulation.context.storage %ctx[4] : !r
      %to = simulation.context.storage %ctx[5] : !r
      %back = simulation.context.storage %ctx[6] : !r
      %out = simulation.context.storage %ctx[7] : !r
      cf.br ^body
    ^body:
      %b = simulation.ref.load %back : !r -> i8
      simulation.ref.store %b to %out : i8, !r
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %to : i8, !r
      simulation.suspend.any %in, %back edges [0, 0] to ^body : !r, !r
    }
    simulation.func @second_inner(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      %in = simulation.context.storage %ctx[5] : !r
      %out = simulation.context.storage %ctx[6] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      simulation.suspend.change %in to ^body : !r
    }
    // first -> observed -> second, and an edge waiter sees observed's output.
    simulation.func @observed(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      %in = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[4] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      simulation.suspend.change %in to ^body : !r
    }
    simulation.func @watcher(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      %w = simulation.context.storage %ctx[4] : !r
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %w to ^tick : !r
    ^tick:
      %out10 = simulation.context.storage %ctx[10] : !r
      %one10 = arith.constant 1 : i8
      simulation.ref.store %one10 to %out10 : i8, !r
      cf.br ^wait
    }
    // @bridge reads both cohorts' outputs, which joins their islands.
    simulation.func @bridge(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64} {
      %in = simulation.context.storage %ctx[3] : !r
      %out = simulation.context.storage %ctx[8] : !r
      %in2 = simulation.context.storage %ctx[7] : !r
      %out2 = simulation.context.storage %ctx[9] : !r
      cf.br ^body
    ^body:
      %a = simulation.ref.load %in : !r -> i8
      simulation.ref.store %a to %out : i8, !r
      %b = simulation.ref.load %in2 : !r -> i8
      simulation.ref.store %b to %out2 : i8, !r
      simulation.suspend.any %in, %in2 edges [0, 0] to ^body : !r, !r
    }
  }
}
