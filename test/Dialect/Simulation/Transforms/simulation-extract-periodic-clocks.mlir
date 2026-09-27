// RUN: obelisk-opt %s --obelisk-sim-extract-periodic-clocks | FileCheck %s --check-prefix=EXTRACT
// RUN: obelisk-opt %s --obelisk-sim-extract-periodic-clocks -o %t.once
// RUN: obelisk-opt %t.once --obelisk-sim-extract-periodic-clocks -o %t.twice
// RUN: diff -u %t.once %t.twice
// RUN: obelisk-opt %s --obelisk-sim-extract-periodic-clocks='vpi=read' | FileCheck %s --check-prefix=VPI
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-extract-periodic-clocks,obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-materialize-clocked-control,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=PLAN

// RSD's two-phase NBA clock, including cold initialization and Active-region
// counter updates. Extract timing without changing the public waveform or
// moving the counters after the public clock's NBA-triggered consumers.
// A second copy uses a period of 5, so checkpoints coincide at time 40.
// Runtime comparison with the untransformed IR is in Runtime/.
// EXTRACT: obelisk_sim.spawn @__obelisk_periodic_tick_
// EXTRACT-LABEL: obelisk_sim.func private @clock
// EXTRACT-SAME: obelisk_sim.periodic_control
// EXTRACT: obelisk_sim.nba.enqueue
// EXTRACT: obelisk_sim.suspend.edge both
// EXTRACT: obelisk_sim.nba.enqueue
// EXTRACT: obelisk_sim.suspend.edge both
// EXTRACT: obelisk_sim.nba.enqueue
// EXTRACT: obelisk_sim.ref.store
// EXTRACT: obelisk_sim.ref.store
// EXTRACT: obelisk_sim.storage.decl 9 in 0 : i1 design {observability = 0 : i32}
// EXTRACT-LABEL: obelisk_sim.func private @__obelisk_periodic_tick_
// EXTRACT: obelisk_sim.time.constant 8
// EXTRACT: obelisk_sim.suspend.delay
// EXTRACT: obelisk_sim.ref.load
// EXTRACT: arith.xori
// EXTRACT: obelisk_sim.ref.store
// VPI-NOT: obelisk_sim.periodic_control
// VPI-NOT: __obelisk_periodic_tick_
// PLAN: __obelisk_periodic_clock_plan_v1
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @rsd_clock {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "snapshot"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "sample"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "slow_clock"
    obelisk_sim.code_unit.decl 6 in 0 always hierarchy "slow_sample"
    obelisk_sim.storage.decl 5 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 6 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 7 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 8 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %r = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %c = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %x = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %y = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %sample = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %r : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %observe = obelisk_sim.spawn @snapshot(%ctx, %c, %x, %y, %sample) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %s = obelisk_sim.spawn @sample(%ctx, %c, %x, %sample) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %p = obelisk_sim.spawn @clock(%ctx, %r, %c, %x, %y) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %slow = obelisk_sim.context.storage %ctx[5] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %sx = obelisk_sim.context.storage %ctx[6] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %sy = obelisk_sim.context.storage %ctx[7] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %ss = obelisk_sim.context.storage %ctx[8] : !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %sc = obelisk_sim.spawn @slow_clock(%ctx, %r, %slow, %sx, %sy) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      %sp = obelisk_sim.spawn @slow_sample(%ctx, %slow, %sx, %ss) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @clock(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %arg1: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %arg2: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %arg3: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %arg4: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {code_unit_id = 2 : i64, domain = 0 : i32, entry_kind = 1 : i32, home_region = 2 : i32, obelisk_sim.hierarchical_name = "TestMain.clkgen"} {
      %0 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %1 = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %2 = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %3 = obelisk_sim.time.constant 8
      %4 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %4 to %arg2 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      obelisk_sim.nba.enqueue %2 to %arg3 : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      obelisk_sim.nba.enqueue %1 to %arg4 : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^bb1
    ^bb1:  // 3 preds: ^bb0, ^bb3, ^bb4
      %5 = obelisk_sim.time.constant 8 {obelisk_sim.rematerialized}
      obelisk_sim.suspend.delay %5 to ^bb2 {site = #obelisk_sim.continuation<id = 9361>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^bb2:  // pred: ^bb1
      %6 = obelisk_sim.logic.constant false, false {obelisk_sim.rematerialized} : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %6 to %arg2 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %7 = obelisk_sim.time.constant 8 {obelisk_sim.rematerialized}
      obelisk_sim.suspend.delay %7 to ^bb3 {site = #obelisk_sim.continuation<id = 9362>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^bb3:  // pred: ^bb2
      %8 = obelisk_sim.logic.constant true, false {obelisk_sim.rematerialized} : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %8 to %arg2 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %9 = obelisk_sim.ref.load %arg1 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %10 = obelisk_sim.logic.constant false, false {obelisk_sim.rematerialized} : !obelisk_sim.logic<1>
      %11 = obelisk_sim.logic.compare eq %9, %10 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      %12 = obelisk_sim.logic.is_true %11 : !obelisk_sim.logic<1>
      cf.cond_br %12, ^bb4, ^bb1
    ^bb4:  // pred: ^bb3
      %13 = obelisk_sim.ref.load %arg3 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %14 = obelisk_sim.logic.constant 1 : i32, 0 : i32 {obelisk_sim.rematerialized} : !obelisk_sim.logic<32>
      %15 = obelisk_sim.logic.binary add %13, %14 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %15 to %arg3 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %16 = obelisk_sim.ref.load %arg4 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %17 = obelisk_sim.logic.constant 1 : i32, 0 : i32 {obelisk_sim.rematerialized} : !obelisk_sim.logic<32>
      %18 = obelisk_sim.logic.binary add %16, %17 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %18 to %arg4 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      cf.br ^bb1
    }
    obelisk_sim.func @snapshot(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %c: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %x: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %y: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %s: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %d0 = obelisk_sim.time.constant 9
      obelisk_sim.suspend.delay %d0 to ^show0
    ^show0:
      %t0 = obelisk_sim.time.now %ctx
      %channel0 = arith.constant 1 : i32
      %fmt0 = obelisk_sim.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c0 = obelisk_sim.ref.load %c : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x0 = obelisk_sim.ref.load %x : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %y0 = obelisk_sim.ref.load %y : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %s0 = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %channel0(%fmt0, %t0, %c0, %x0, %y0, %s0) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      cf.br ^point1
    ^point1:
      %d1 = obelisk_sim.time.constant 8
      obelisk_sim.suspend.delay %d1 to ^show1
    ^show1:
      %t1 = obelisk_sim.time.now %ctx
      %channel1 = arith.constant 1 : i32
      %fmt1 = obelisk_sim.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c1 = obelisk_sim.ref.load %c : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x1 = obelisk_sim.ref.load %x : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %y1 = obelisk_sim.ref.load %y : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %s1 = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %channel1(%fmt1, %t1, %c1, %x1, %y1, %s1) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      cf.br ^point2
    ^point2:
      %d2 = obelisk_sim.time.constant 16
      obelisk_sim.suspend.delay %d2 to ^show2
    ^show2:
      %t2 = obelisk_sim.time.now %ctx
      %channel2 = arith.constant 1 : i32
      %fmt2 = obelisk_sim.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c2 = obelisk_sim.ref.load %c : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x2 = obelisk_sim.ref.load %x : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %y2 = obelisk_sim.ref.load %y : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %s2 = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %channel2(%fmt2, %t2, %c2, %x2, %y2, %s2) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      cf.br ^point3
    ^point3:
      %d3 = obelisk_sim.time.constant 32
      obelisk_sim.suspend.delay %d3 to ^show3
    ^show3:
      %t3 = obelisk_sim.time.now %ctx
      %channel3 = arith.constant 1 : i32
      %fmt3 = obelisk_sim.bytes.constant "t=%0d clock=%0d cycle=%0d kanata=%0d sampled=%0d"
      %c3 = obelisk_sim.ref.load %c : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x3 = obelisk_sim.ref.load %x : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %y3 = obelisk_sim.ref.load %y : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %s3 = obelisk_sim.ref.load %s : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %channel3(%fmt3, %t3, %c3, %x3, %y3, %s3) newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %status = arith.constant 1 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
    obelisk_sim.func @sample(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %c: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %x: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %s: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %c to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %value = obelisk_sim.ref.load %x : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.nba.enqueue %value to %s : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^wait
    }
    obelisk_sim.func private @slow_clock(%arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %arg1: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %arg2: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64}, %arg3: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 6 : i64}, %arg4: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 7 : i64}) attributes {code_unit_id = 5 : i64, domain = 0 : i32, entry_kind = 1 : i32, home_region = 2 : i32, obelisk_sim.hierarchical_name = "slow_clock"} {
      %0 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %1 = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %2 = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %3 = obelisk_sim.time.constant 5
      %4 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %4 to %arg2 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      obelisk_sim.nba.enqueue %2 to %arg3 : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      obelisk_sim.nba.enqueue %1 to %arg4 : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^bb1
    ^bb1:  // 3 preds: ^bb0, ^bb3, ^bb4
      %5 = obelisk_sim.time.constant 5 {obelisk_sim.rematerialized}
      obelisk_sim.suspend.delay %5 to ^bb2 {site = #obelisk_sim.continuation<id = 9361>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^bb2:  // pred: ^bb1
      %6 = obelisk_sim.logic.constant false, false {obelisk_sim.rematerialized} : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %6 to %arg2 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %7 = obelisk_sim.time.constant 5 {obelisk_sim.rematerialized}
      obelisk_sim.suspend.delay %7 to ^bb3 {site = #obelisk_sim.continuation<id = 9362>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^bb3:  // pred: ^bb2
      %8 = obelisk_sim.logic.constant true, false {obelisk_sim.rematerialized} : !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %8 to %arg2 : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %9 = obelisk_sim.ref.load %arg1 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %10 = obelisk_sim.logic.constant false, false {obelisk_sim.rematerialized} : !obelisk_sim.logic<1>
      %11 = obelisk_sim.logic.compare eq %9, %10 : (!obelisk_sim.logic<1>, !obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      %12 = obelisk_sim.logic.is_true %11 : !obelisk_sim.logic<1>
      cf.cond_br %12, ^bb4, ^bb1
    ^bb4:  // pred: ^bb3
      %13 = obelisk_sim.ref.load %arg3 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %14 = obelisk_sim.logic.constant 1 : i32, 0 : i32 {obelisk_sim.rematerialized} : !obelisk_sim.logic<32>
      %15 = obelisk_sim.logic.binary add %13, %14 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %15 to %arg3 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %16 = obelisk_sim.ref.load %arg4 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %17 = obelisk_sim.logic.constant 1 : i32, 0 : i32 {obelisk_sim.rematerialized} : !obelisk_sim.logic<32>
      %18 = obelisk_sim.logic.binary add %16, %17 : !obelisk_sim.logic<32>
      obelisk_sim.ref.store %18 to %arg4 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      cf.br ^bb1
    }
    obelisk_sim.func @slow_sample(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %c: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64},
        %x: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 6 : i64},
        %s: !obelisk_sim.ref<!obelisk_sim.logic<32>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 8 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %c to ^body : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %value = obelisk_sim.ref.load %x : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.nba.enqueue %value to %s : (!obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>) -> ()
      cf.br ^wait
    }
  }
}
