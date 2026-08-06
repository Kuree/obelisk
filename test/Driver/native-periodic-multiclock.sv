// RUN: obelisk -O2 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O2 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O2 --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -O2 -emit-llvm --native-scheduler=eval %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %t.auto > %t.auto.out
// RUN: %t.eval > %t.eval.out
// RUN: %t.generic > %t.generic.out
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval > /dev/null 2> %t.eval.diag
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s < %t.auto.out
// RUN: FileCheck %s --check-prefix=DIAG < %t.eval.diag

// Periods 4 and 12 produce coincident edges at time 6, 18, and 30. Both source
// bits share one packed physical root and cross separate port projections;
// this catches alias plans that identify a clock by descriptor but omit its
// packed offset. The shared NBA root makes same-slot process ordering
// observable, while the independent counters catch lost or duplicated edges.
module clock_sink(input bit clock2, input bit clock3, input bit clock2n);
  int count2;
  int count3;
  int count2n;
  int shared;
  int either;

  always @(posedge clock2) begin
    count2 <= count2 + 1;
    shared <= shared + 1;
  end

  always @(posedge clock3) begin
    count3 <= count3 + 1;
    shared <= shared + 10;
  end

  // This falling edge is phase-aligned with clock2's rising edge. It checks
  // that physical trigger grouping includes edge polarity instead of folding
  // differential clocks by period alone.
  always @(negedge clock2n)
    count2n <= count2n + 1;

  // Coincident publications must enqueue one activation, even though both
  // sensitivity entries match in the same slot.
  always @(posedge clock2 or posedge clock3)
    either <= either + 1;
endmodule

module native_periodic_multiclock;
  bit [1:0] clocks;
  bit clock2n = 1'b1;
  clock_sink sink(.clock2(clocks[0]), .clock3(clocks[1]),
                  .clock2n(clock2n));

  always #2 clocks[0] = ~clocks[0];
  always #6 clocks[1] = ~clocks[1];
  always #2 clock2n = ~clock2n;

  // Exercise resumable Tier-3 work between physical edges. The saved clocks
  // must retain absolute phases 6 and 6 when run_until resumes at time 5.
  initial begin
    #5;
    $display("offedge count2=%0d count3=%0d", sink.count2, sink.count3);
  end

  // Land a Tier-3 continuation on the exact t=6 edge shared by both clocks.
  // It must be ordered against the two native clock owners with the same
  // region/rank rules as the generic scheduler, without permanently
  // deoptimizing the periodic schedule.
  initial begin
    #6;
    sink.shared = 100;
    $display("coincident preedge shared=%0d", sink.shared);
  end

  initial begin
    #31;
    $display("multiclock count2=%0d count3=%0d count2n=%0d shared=%0d either=%0d",
             sink.count2, sink.count3, sink.count2n, sink.shared, sink.either);
    $finish;
  end
endmodule

// CHECK: offedge count2=1 count3=0
// CHECK: coincident preedge shared=100
// CHECK: multiclock count2=8 count3=3 count2n=8 shared=35 either=8

// DIAG: obelisk-signal-diagnostics {{.*}}aot_fallbacks=0

// Each outlined owner has an independent promotion latch.  The generated
// scanner is a local masked-plane check and has no runtime edge.
// LLVM-DAG: @__obelisk_eval_kernel_promotion_latched_v1 = internal {{.*}}global [7 x i8] zeroinitializer
// LLVM-DAG: @__obelisk_eval_promotion_pending_mask_v1 = internal {{.*}}global i64 127
// LLVM-DAG: @__obelisk_eval_periodic_entry_promotion_latched_v1 = internal {{.*}}global i{{1|8}} {{false|0}}
// LLVM-LABEL: define {{.*}}i1 @__obelisk_eval_kernel_promotion_ready_v1_0
// LLVM-NOT: call {{.*}}@obelisk_rt_
// LLVM: load i8, ptr @__obelisk_eval_kernel_promotion_latched_v1
// LLVM: ret i1 true
// LLVM: store i8 1, ptr @__obelisk_eval_kernel_promotion_latched_v1

// The aggregate boundary scans the exact closure once, then observes only
// owner-ready bits. Dormant owners are not rescanned on every physical edge;
// selected pending owners clear their own bit in the hybrid coordinator.
// LLVM-LABEL: define {{.*}}i1 @__obelisk_eval_periodic_promotion_ready_v1
// LLVM: load i{{1|8}}, ptr @__obelisk_eval_periodic_promotion_scanned_v1
// LLVM: load i64, ptr @__obelisk_eval_promotion_pending_mask_v1
// LLVM: icmp eq i64
// LLVM: store i8 {{.*}}, ptr @__obelisk_eval_periodic_entry_promotion_latched_v1
// LLVM: ret i1

// Promotion invalidation is a cold generated store. It contains no runtime
// edge; the subsequent quiescent coordinator scan selects four- or two-state.
// LLVM-LABEL: define void @__obelisk_eval_promotion_invalidate_v1
// LLVM-NOT: call {{.*}}@obelisk_rt_
// LLVM: store i8 {{.*}}, ptr @__obelisk_eval_promotion_latched_v1
// LLVM: store i8 {{.*}}, ptr @__obelisk_eval_periodic_promotion_latched_v1
// LLVM: store i8 {{.*}}, ptr @__obelisk_eval_periodic_entry_promotion_latched_v1
// LLVM: call void @llvm.memset{{.*}}@__obelisk_eval_kernel_promotion_latched_v1
// LLVM: store i64 127, ptr @__obelisk_eval_promotion_pending_mask_v1
// LLVM: store i8 {{.*}}, ptr @__obelisk_eval_fast_nba_latched_v1
// LLVM: ret void

// The post-transient coordinator checks the ready mask against pending owners
// locally and contains no runtime edge on its normal Tier-1/Tier-2 path.
// LLVM-LABEL: define {{.*}}i32 @__obelisk_eval_steady_two_state_coordinator_v1
// LLVM-NOT: call {{.*}}@obelisk_rt_
// LLVM: load i64, ptr @__obelisk_eval_promotion_pending_mask_v1
// The transient/guarded coordinator deliberately keeps a mutable owner edge.
// LLVM: call void %{{.*}}(ptr %{{.*}})
// LLVM: or i64
// LLVM: and i64
// LLVM: ret i32

// Once the complete periodic closure promotes, its specialized wrapper has a
// static two-state body edge. This is what lets normal LLVM profitability
// inline small module instances while leaving large bodies out of line.
// LLVM-LABEL: define {{.*}}i32 @{{.*}}.__obelisk_trusted
// LLVM-NOT: @__obelisk_eval_function_route_v1_
// LLVM: ret i32 0
