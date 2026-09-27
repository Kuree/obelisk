// RUN: obelisk-opt %s --obelisk-sim-extract-periodic-clocks | FileCheck %s --check-prefix=EXTRACT
// RUN: obelisk-opt %s --obelisk-sim-extract-periodic-clocks -o %t.once
// RUN: obelisk-opt %t.once --obelisk-sim-extract-periodic-clocks -o %t.twice
// RUN: diff -u %t.once %t.twice
// RUN: obelisk-opt %s --obelisk-sim-extract-periodic-clocks='vpi=read' | FileCheck %s --check-prefix=VPI
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-extract-periodic-clocks,simulation.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=PLAN

// RSD's two-phase NBA clock, including cold initialization and Active-region
// counter updates. Extract timing without changing the public waveform or
// moving the counters after the public clock's NBA-triggered consumers.
// A second copy uses a period of 5, so checkpoints coincide at time 40.
// Runtime comparison with the untransformed IR is in Runtime/.
// EXTRACT: simulation.spawn @__obelisk_periodic_tick_
// EXTRACT-LABEL: simulation.func private @clock
// EXTRACT-SAME: schedule.periodic_control
// EXTRACT: simulation.nba.enqueue
// EXTRACT: simulation.suspend.edge both
// EXTRACT: simulation.nba.enqueue
// EXTRACT: simulation.suspend.edge both
// EXTRACT: simulation.nba.enqueue
// EXTRACT: simulation.ref.store
// EXTRACT: simulation.ref.store
// EXTRACT: simulation.storage.decl 9 in 0 : i1 design {observability = 0 : i32}
// EXTRACT-LABEL: simulation.func private @__obelisk_periodic_tick_
// EXTRACT: simulation.time.constant 8
// EXTRACT: simulation.suspend.delay
// EXTRACT: simulation.ref.load
// EXTRACT: arith.xori
// EXTRACT: simulation.ref.store
// VPI-NOT: schedule.periodic_control
// VPI-NOT: __obelisk_periodic_tick_
// PLAN: __obelisk_periodic_clock_plan_v1
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @rsd_clock {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "clock"
    simulation.code_unit.decl 3 in 0 initial hierarchy "snapshot"
    simulation.code_unit.decl 4 in 0 always hierarchy "sample"
    simulation.code_unit.decl 5 in 0 initial hierarchy "slow_clock"
    simulation.code_unit.decl 6 in 0 always hierarchy "slow_sample"
    simulation.storage.decl 5 in 0 : !simulation.logic<1> design
    simulation.storage.decl 6 in 0 : !simulation.logic<32> design
    simulation.storage.decl 7 in 0 : !simulation.logic<32> design
    simulation.storage.decl 8 in 0 : !simulation.logic<32> design
    simulation.storage.decl 4 in 0 : !simulation.logic<32> design
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<32> design
    simulation.storage.decl 3 in 0 : !simulation.logic<32> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %r = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %c = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %x = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<32>>
      %y = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<32>>
      %sample = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<32>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %r : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %observe = simulation.spawn @snapshot(%ctx, %c, %x, %y, %sample) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %s = simulation.spawn @sample(%ctx, %c, %x, %sample) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %p = simulation.spawn @clock(%ctx, %r, %c, %x, %y) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %slow = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<1>>
      %sx = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<32>>
      %sy = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<32>>
      %ss = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<32>>
      %sc = simulation.spawn @slow_clock(%ctx, %r, %slow, %sx, %sy) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      %sp = simulation.spawn @slow_sample(%ctx, %slow, %sx, %ss) : !simulation.context, !simulation.ref<!simulation.logic<1>>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<32>> -> !simulation.process
      simulation.return
    }
    simulation.func private @clock(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %arg3: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %arg4: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) attributes {code_unit_id = 2 : i64, domain = 0 : i32, entry_kind = 1 : i32, home_region = 2 : i32, simulation.hierarchical_name = "TestMain.clkgen"} {
      %0 = simulation.logic.constant true, false : !simulation.logic<1>
      %1 = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %2 = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      %3 = simulation.time.constant 8
      %4 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.nba.enqueue %4 to %arg2 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      simulation.nba.enqueue %2 to %arg3 : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      simulation.nba.enqueue %1 to %arg4 : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^bb1
    ^bb1:  // 3 preds: ^bb0, ^bb3, ^bb4
      %5 = simulation.time.constant 8 {simulation.rematerialized}
      simulation.suspend.delay %5 to ^bb2 {site = #schedule.continuation<id = 9361>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^bb2:  // pred: ^bb1
      %6 = simulation.logic.constant false, false {simulation.rematerialized} : !simulation.logic<1>
      simulation.nba.enqueue %6 to %arg2 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %7 = simulation.time.constant 8 {simulation.rematerialized}
      simulation.suspend.delay %7 to ^bb3 {site = #schedule.continuation<id = 9362>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^bb3:  // pred: ^bb2
      %8 = simulation.logic.constant true, false {simulation.rematerialized} : !simulation.logic<1>
      simulation.nba.enqueue %8 to %arg2 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %9 = simulation.ref.load %arg1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %10 = simulation.logic.constant false, false {simulation.rematerialized} : !simulation.logic<1>
      %11 = simulation.logic.compare eq %9, %10 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
      %12 = simulation.logic.is_true %11 : !simulation.logic<1>
      cf.cond_br %12, ^bb4, ^bb1
    ^bb4:  // pred: ^bb3
      %13 = simulation.ref.load %arg3 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %14 = simulation.logic.constant 1 : i32, 0 : i32 {simulation.rematerialized} : !simulation.logic<32>
      %15 = simulation.logic.binary add %13, %14 : !simulation.logic<32>
      simulation.ref.store %15 to %arg3 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %16 = simulation.ref.load %arg4 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %17 = simulation.logic.constant 1 : i32, 0 : i32 {simulation.rematerialized} : !simulation.logic<32>
      %18 = simulation.logic.binary add %16, %17 : !simulation.logic<32>
      simulation.ref.store %18 to %arg4 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^bb1
    }
    simulation.func @snapshot(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %c: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %x: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %y: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
        %s: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %d0 = simulation.time.constant 9
      simulation.suspend.delay %d0 to ^show0
    ^show0:
      %t0 = simulation.time.now %ctx
      %channel0 = arith.constant 1 : i32
      %fmt0 = simulation.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c0 = simulation.ref.load %c : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %x0 = simulation.ref.load %x : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %y0 = simulation.ref.load %y : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %s0 = simulation.ref.load %s : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %channel0(%fmt0, %t0, %c0, %x0, %y0, %s0) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<32>, !simulation.logic<32>, !simulation.logic<32>
      cf.br ^point1
    ^point1:
      %d1 = simulation.time.constant 8
      simulation.suspend.delay %d1 to ^show1
    ^show1:
      %t1 = simulation.time.now %ctx
      %channel1 = arith.constant 1 : i32
      %fmt1 = simulation.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c1 = simulation.ref.load %c : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %x1 = simulation.ref.load %x : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %y1 = simulation.ref.load %y : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %s1 = simulation.ref.load %s : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %channel1(%fmt1, %t1, %c1, %x1, %y1, %s1) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<32>, !simulation.logic<32>, !simulation.logic<32>
      cf.br ^point2
    ^point2:
      %d2 = simulation.time.constant 16
      simulation.suspend.delay %d2 to ^show2
    ^show2:
      %t2 = simulation.time.now %ctx
      %channel2 = arith.constant 1 : i32
      %fmt2 = simulation.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c2 = simulation.ref.load %c : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %x2 = simulation.ref.load %x : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %y2 = simulation.ref.load %y : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %s2 = simulation.ref.load %s : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %channel2(%fmt2, %t2, %c2, %x2, %y2, %s2) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<32>, !simulation.logic<32>, !simulation.logic<32>
      cf.br ^point3
    ^point3:
      %d3 = simulation.time.constant 32
      simulation.suspend.delay %d3 to ^show3
    ^show3:
      %t3 = simulation.time.now %ctx
      %channel3 = arith.constant 1 : i32
      %fmt3 = simulation.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c3 = simulation.ref.load %c : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %x3 = simulation.ref.load %x : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %y3 = simulation.ref.load %y : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %s3 = simulation.ref.load %s : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.display %ctx to %channel3(%fmt3, %t3, %c3, %x3, %y3, %s3) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, i64, !simulation.logic<1>, !simulation.logic<32>, !simulation.logic<32>, !simulation.logic<32>
      %status = arith.constant 1 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %c: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %x: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
        %s: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %c to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %value = simulation.ref.load %x : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.nba.enqueue %value to %s : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^wait
    }
    simulation.func private @slow_clock(%arg0: !simulation.context {simulation.capture_kind = 0 : i32}, %arg1: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %arg2: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}, %arg3: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64}, %arg4: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {code_unit_id = 5 : i64, domain = 0 : i32, entry_kind = 1 : i32, home_region = 2 : i32, simulation.hierarchical_name = "slow_clock"} {
      %0 = simulation.logic.constant true, false : !simulation.logic<1>
      %1 = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %2 = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      %3 = simulation.time.constant 5
      %4 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.nba.enqueue %4 to %arg2 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      simulation.nba.enqueue %2 to %arg3 : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      simulation.nba.enqueue %1 to %arg4 : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^bb1
    ^bb1:  // 3 preds: ^bb0, ^bb3, ^bb4
      %5 = simulation.time.constant 5 {simulation.rematerialized}
      simulation.suspend.delay %5 to ^bb2 {site = #schedule.continuation<id = 9361>, timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^bb2:  // pred: ^bb1
      %6 = simulation.logic.constant false, false {simulation.rematerialized} : !simulation.logic<1>
      simulation.nba.enqueue %6 to %arg2 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %7 = simulation.time.constant 5 {simulation.rematerialized}
      simulation.suspend.delay %7 to ^bb3 {site = #schedule.continuation<id = 9362>, timing = #schedule.timing_site<id = 1, kind = calendar>}
    ^bb3:  // pred: ^bb2
      %8 = simulation.logic.constant true, false {simulation.rematerialized} : !simulation.logic<1>
      simulation.nba.enqueue %8 to %arg2 : (!simulation.logic<1>, !simulation.ref<!simulation.logic<1>>) -> ()
      %9 = simulation.ref.load %arg1 : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %10 = simulation.logic.constant false, false {simulation.rematerialized} : !simulation.logic<1>
      %11 = simulation.logic.compare eq %9, %10 : (!simulation.logic<1>, !simulation.logic<1>) -> !simulation.logic<1>
      %12 = simulation.logic.is_true %11 : !simulation.logic<1>
      cf.cond_br %12, ^bb4, ^bb1
    ^bb4:  // pred: ^bb3
      %13 = simulation.ref.load %arg3 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %14 = simulation.logic.constant 1 : i32, 0 : i32 {simulation.rematerialized} : !simulation.logic<32>
      %15 = simulation.logic.binary add %13, %14 : !simulation.logic<32>
      simulation.ref.store %15 to %arg3 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %16 = simulation.ref.load %arg4 : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %17 = simulation.logic.constant 1 : i32, 0 : i32 {simulation.rematerialized} : !simulation.logic<32>
      %18 = simulation.logic.binary add %16, %17 : !simulation.logic<32>
      simulation.ref.store %18 to %arg4 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      cf.br ^bb1
    }
    simulation.func @slow_sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %c: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64},
        %x: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64},
        %s: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %c to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %value = simulation.ref.load %x : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      simulation.nba.enqueue %value to %s : (!simulation.logic<32>, !simulation.ref<!simulation.logic<32>>) -> ()
      cf.br ^wait
    }
  }
}
