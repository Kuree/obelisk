// RUN: talos-opt %s -split-input-file -verify-diagnostics

// A periodic collection point records both its clock and its period.
llvm.func @clock_without_period(%ctx: !llvm.ptr) -> i32 {
  // expected-error @+1 {{a periodic collection point needs both its clock and its period}}
  %s = schedule.gc.collect %ctx {periodic_clock = 0 : i32} : !llvm.ptr
  llvm.return %s : i32
}

// -----

llvm.func @period_without_clock(%ctx: !llvm.ptr) -> i32 {
  // expected-error @+1 {{a periodic collection point needs both its clock and its period}}
  %s = schedule.gc.collect %ctx {period = 10 : i64} : !llvm.ptr
  llvm.return %s : i32
}

// -----

llvm.func @zero_period(%ctx: !llvm.ptr) -> i32 {
  // expected-error @+1 {{a periodic clock has a positive period}}
  %s = schedule.gc.collect %ctx {periodic_clock = 0 : i32, period = 0 : i64} : !llvm.ptr
  llvm.return %s : i32
}

// -----

// Only a point that follows a periodic clock is guarded by one of its edges.
llvm.func @edge_without_clock(%ctx: !llvm.ptr, %edge: i1) -> i32 {
  // expected-error @+1 {{only a periodic collection point follows an edge}}
  %s = schedule.gc.collect %ctx if %edge : !llvm.ptr
  llvm.return %s : i32
}

// -----

llvm.func @posedge_without_clock(%ctx: !llvm.ptr) -> i32 {
  // expected-error @+1 {{only a periodic collection point follows an edge}}
  %s = schedule.gc.collect %ctx {posedge} : !llvm.ptr
  llvm.return %s : i32
}
