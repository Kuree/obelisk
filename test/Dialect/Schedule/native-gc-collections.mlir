// RUN: obelisk-opt %s --schedule-thin-gc-collections | FileCheck %s --check-prefix=THIN
// RUN: obelisk-opt %s --schedule-thin-gc-collections --schedule-lower-gc-collections | FileCheck %s --check-prefix=LOWER

// A generated periodic loop designates a collection point after every edge of
// every proven periodic clock and at every slot end. IEEE 1800-2023 8.29
// leaves the timing of reclamation to the implementation, so one point per
// cycle suffices: the rising edge of the fastest clock. Removed points report
// success, and their status checks fold into plain branches that rejoin the
// block they split.

module {
  // THIN-LABEL: llvm.func @loop(
  // THIN-SAME: %[[CTX:.*]]: !llvm.ptr, %[[RISE0:.*]]: i1, %[[FALL0:.*]]: i1, %[[RISE1:.*]]: i1, %[[FALL1:.*]]: i1)
  // THIN-NOT: schedule.gc.collect
  // THIN: %[[KEPT:.*]] = schedule.gc.collect %[[CTX]] if %[[RISE1]] {period = 6 : i64, periodic_clock = 1 : i32, posedge} : !llvm.ptr
  // THIN-NEXT: %[[OK:.*]] = arith.cmpi eq, %[[KEPT]], %{{.*}} : i32
  // THIN-NEXT: cf.cond_br %[[OK]], ^[[AFTER:bb[0-9]+]], ^[[FAIL:bb[0-9]+]](%[[KEPT]] : i32)
  // THIN-NOT: schedule.gc.collect
  // THIN: llvm.return
  // LOWER-LABEL: llvm.func @loop(
  // LOWER-SAME: %[[CTX:.*]]: !llvm.ptr, %{{.*}}: i1, %{{.*}}: i1, %[[RISE1:.*]]: i1, %{{.*}}: i1)
  // LOWER-NEXT: arith.constant 0 : i32
  // A slot without the edge skips the call and reports success.
  // LOWER: %[[SKIPPED:.*]] = arith.constant 0 : i32
  // LOWER-NEXT: cf.cond_br %[[RISE1]], ^[[COLLECT:bb[0-9]+]], ^[[JOIN:bb[0-9]+]](%[[SKIPPED]] : i32)
  // LOWER: ^[[COLLECT]]:
  // LOWER-NEXT: %[[RT:.*]] = builtin.unrealized_conversion_cast %[[CTX]] : !llvm.ptr to !runtime.context
  // LOWER-NEXT: %[[STATUS:.*]] = runtime.gc.collect
  // LOWER-NEXT: %[[BITS:.*]] = runtime.status.to_bits %[[STATUS]]
  // LOWER-NEXT: cf.br ^[[JOIN]](%[[BITS]] : i32)
  // LOWER: ^[[JOIN]](%[[RESULT:.*]]: i32):
  // LOWER-NEXT: arith.cmpi eq, %[[RESULT]]
  // LOWER-NOT: schedule.gc.collect
  llvm.func @loop(%ctx: !llvm.ptr, %rise0: i1, %fall0: i1, %rise1: i1, %fall1: i1) -> i32 {
    %ok = arith.constant 0 : i32
    %s0 = schedule.gc.collect %ctx if %rise0 {periodic_clock = 0 : i32, period = 10 : i64, posedge} : !llvm.ptr
    %c0 = arith.cmpi eq, %s0, %ok : i32
    cf.cond_br %c0, ^fall0, ^fail(%s0 : i32)
  ^fall0:
    %s1 = schedule.gc.collect %ctx if %fall0 {periodic_clock = 0 : i32, period = 10 : i64} : !llvm.ptr
    %c1 = arith.cmpi eq, %s1, %ok : i32
    cf.cond_br %c1, ^rise1, ^fail(%s1 : i32)
  ^rise1:
    %s2 = schedule.gc.collect %ctx if %rise1 {periodic_clock = 1 : i32, period = 6 : i64, posedge} : !llvm.ptr
    %c2 = arith.cmpi eq, %s2, %ok : i32
    cf.cond_br %c2, ^fall1, ^fail(%s2 : i32)
  ^fall1:
    %s3 = schedule.gc.collect %ctx if %fall1 {periodic_clock = 1 : i32, period = 6 : i64} : !llvm.ptr
    %c3 = arith.cmpi eq, %s3, %ok : i32
    cf.cond_br %c3, ^slot, ^fail(%s3 : i32)
  ^slot:
    %s4 = schedule.gc.collect %ctx : !llvm.ptr
    %c4 = arith.cmpi eq, %s4, %ok : i32
    cf.cond_br %c4, ^done, ^fail(%s4 : i32)
  ^done:
    llvm.return %ok : i32
  ^fail(%status: i32):
    llvm.return %status : i32
  }

  // Without a periodic clock nothing proves a cycle, so every point stays.
  // THIN-LABEL: llvm.func @unclocked(
  // THIN: schedule.gc.collect %{{.*}} : !llvm.ptr
  // THIN: schedule.gc.collect %{{.*}} : !llvm.ptr
  // LOWER-LABEL: llvm.func @unclocked(
  // LOWER-COUNT-2: runtime.gc.collect
  // LOWER-NOT: schedule.gc.collect
  llvm.func @unclocked(%ctx: !llvm.ptr) -> i32 {
    %a = schedule.gc.collect %ctx : !llvm.ptr
    %b = schedule.gc.collect %ctx : !llvm.ptr
    %sum = arith.ori %a, %b : i32
    llvm.return %sum : i32
  }
}
