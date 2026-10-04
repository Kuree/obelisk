// RUN: obelisk-opt %s --convert-native-schedule-actions-to-llvm -o %t.lowered
// RUN: FileCheck %s --implicit-check-not=llvm.alloca < %t.lowered
// RUN: %llvm_dist/bin/mlir-opt %t.lowered --convert-arith-to-llvm --convert-cf-to-llvm | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/clang -O0 %t.ll -o %t.o0
// RUN: %t.o0
// RUN: %llvm_dist/bin/clang -O3 %t.ll -o %t.o3
// RUN: %t.o3
// Region-local accounting and pending consumers stay in SSA.
// Ordered overlapping slots and knownness callbacks see intermediate state;
// round-trip changes still wake consumers. Sparse dirty sets, a later barrier,
// guard failure and adjacent untouched bits retain their original behavior.
// CHECK-LABEL: llvm.func @exercise
// CHECK-NOT: llvm.alloca
// CHECK-NOT: llvm.alloca
// CHECK-DAG: llvm.call @obelisk_rt_v1_static_nba_batch_knownness
// CHECK-DAG: llvm.call @obelisk_rt_v1_static_nba_batch_finish
#batch = #schedule.nba_batch_layout<plan = @plan, value = @value, unknown = @unknown, dirty = @dirty, summary = @summary, ingress = @ingress, bitCount = 16, rootCount = 4, capacity = 4>
#slot0 = #schedule.nba_slot_layout<offset = @offset, value = @s0, unknown = @u0, valid = @valid0, width = 3, constantOffset = 0 : i64>
#slot1 = #schedule.nba_slot_layout<offset = @offset, value = @zero, unknown = @zero, valid = @valid1, width = 3, constantOffset = 0 : i64>
#slot3 = #schedule.nba_slot_layout<offset = @offset, value = @zero, unknown = @zero, valid = @valid3, width = 4, constantOffset = 0 : i64>
#c0 = #schedule.nba_consumer<low = 0, width = 3, bit = 0>
#c1 = #schedule.nba_consumer<low = 0, width = 4, bit = 1>
#c2 = #schedule.nba_consumer<low = 0, width = 4, bit = 2>
#root0 = #schedule.nba_commit_layout<offset = 1, width = 3, slots = [#slot0, #slot1], consumers = [#c0]>
#root1 = #schedule.nba_commit_layout<offset = 4, width = 4, accumulator = @acc1, slots = [], consumers = [#c1]>
#root2 = #schedule.nba_commit_layout<offset = 8, width = 4, accumulator = @acc2, slots = [], consumers = [#c2]>
#root3 = #schedule.nba_commit_layout<offset = 12, width = 4, slots = [#slot3], consumers = []>
module {
  llvm.mlir.global internal @plan(0 : i8) : i8
  llvm.mlir.global internal @value(42240 : i16) : i16
  llvm.mlir.global internal @unknown(0 : i16) : i16
  llvm.mlir.global internal @dirty(7 : i64) : i64
  llvm.mlir.global internal @summary(1 : i64) : i64
  llvm.mlir.global internal @ingress(0 : i64) : i64
  llvm.mlir.global internal @offset(0 : i64) : i64
  llvm.mlir.global internal @s0(5 : i64) : i64
  llvm.mlir.global internal @u0(3 : i64) : i64
  llvm.mlir.global internal @zero(0 : i64) : i64
  llvm.mlir.global internal @valid0(1 : i32) : i32
  llvm.mlir.global internal @valid1(1 : i32) : i32
  llvm.mlir.global internal @valid3(1 : i32) : i32
  llvm.mlir.global internal @allowed(0 : i32) : i32
  llvm.mlir.global internal @error(0 : i32) : i32
  llvm.mlir.global internal @calls(0 : i64) : i64
  llvm.mlir.global internal @stages(0 : i64) : i64
  llvm.mlir.global internal @commits(0 : i64) : i64
  llvm.mlir.global internal @changed(8 : i32) : i32
  llvm.mlir.global internal @acc1(dense<[0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 8589934593, 1, 0, 0, 0]> : tensor<17xi64>) : !llvm.array<17 x i64>
  llvm.mlir.global internal @acc2(dense<[15, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 17179869185, 0, 0, 0, 0]> : tensor<17xi64>) : !llvm.array<17 x i64>
  llvm.func @obelisk_rt_v1_static_nba_batch_guard(%ctx: !llvm.ptr, %plan: !llvm.ptr, %region: i32) -> i32 {
    %p = llvm.mlir.addressof @allowed : !llvm.ptr
    %v = llvm.load %p : !llvm.ptr -> i32
    llvm.return %v : i32
  }
  llvm.func @require(%ok: i1) {
    %p = llvm.mlir.addressof @error : !llvm.ptr
    %old = llvm.load %p : !llvm.ptr -> i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %next = llvm.select %ok, %old, %one : i1, i32
    llvm.store %next, %p : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @obelisk_rt_v1_static_nba_batch_knownness(%ctx: !llvm.ptr, %low: i64, %width: i64, %old: i64, %new: i64) {
    %vp = llvm.mlir.addressof @value : !llvm.ptr
    %up = llvm.mlir.addressof @unknown : !llvm.ptr
    %v = llvm.load %vp : !llvm.ptr -> i16
    %u = llvm.load %up : !llvm.ptr -> i16
    %new16 = llvm.trunc %new : i64 to i16
    %zero = llvm.mlir.constant(0 : i64) : i64
    %one = llvm.mlir.constant(1 : i16) : i16
    %base = llvm.mlir.constant(42240 : i16) : i16
    %first = llvm.mlir.constant(42250 : i16) : i16
    %nz = llvm.icmp "ne" %new, %zero : i64
    %expected = llvm.select %nz, %first, %base : i1, i16
    %expectedu = llvm.shl %new16, %one : i16
    %a = llvm.icmp "eq" %v, %expected : i16
    %b = llvm.icmp "eq" %u, %expectedu : i16
    %ok = llvm.and %a, %b : i1
    llvm.call @require(%ok) : (i1) -> ()
    %p = llvm.mlir.addressof @calls : !llvm.ptr
    %count = llvm.load %p : !llvm.ptr -> i64
    %inc = llvm.mlir.constant(1 : i64) : i64
    %next = llvm.add %count, %inc : i64
    llvm.store %next, %p : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @obelisk_rt_v1_static_nba_batch_finish(%ctx: !llvm.ptr, %stages: i64, %commits: i64, %pending: i32) {
    %stagesp = llvm.mlir.addressof @stages : !llvm.ptr
    %stagesold = llvm.load %stagesp : !llvm.ptr -> i64
    %stagesnext = llvm.add %stagesold, %stages : i64
    llvm.store %stagesnext, %stagesp : i64, !llvm.ptr
    %commitsp = llvm.mlir.addressof @commits : !llvm.ptr
    %commitsold = llvm.load %commitsp : !llvm.ptr -> i64
    %commitsnext = llvm.add %commitsold, %commits : i64
    llvm.store %commitsnext, %commitsp : i64, !llvm.ptr
    llvm.return
  }
  llvm.func @exercise(%barrier: i32) attributes {passthrough = ["noinline"]} {
    %ctx = llvm.mlir.zero : !llvm.ptr
    %changed = llvm.mlir.addressof @changed : !llvm.ptr
    %status = schedule.nba.batch %ctx, %barrier, %changed {
      schedule.nba.commit 0 {layout = #root0}
      schedule.nba.commit 1 {layout = #root1}
      schedule.nba.commit 2 {layout = #root2}
      schedule.nba.commit 3 {layout = #root3}
      schedule.nba.yield
    } {layout = #batch} : (!llvm.ptr, i32, !llvm.ptr) -> i32
    llvm.return
  }
  llvm.func @main() -> i32 {
    %nba = llvm.mlir.constant(2 : i32) : i32
    %later = llvm.mlir.constant(4 : i32) : i32
    llvm.call @exercise(%nba) : (i32) -> ()
    %p1 = llvm.mlir.addressof @dirty : !llvm.ptr
    %v1 = llvm.load %p1 : !llvm.ptr -> i64
    %c1 = llvm.mlir.constant(7 : i64) : i64
    %ok1 = llvm.icmp "eq" %v1, %c1 : i64
    llvm.call @require(%ok1) : (i1) -> ()
    %p2 = llvm.mlir.addressof @value : !llvm.ptr
    %v2 = llvm.load %p2 : !llvm.ptr -> i16
    %c2 = llvm.mlir.constant(42240 : i16) : i16
    %ok2 = llvm.icmp "eq" %v2, %c2 : i16
    llvm.call @require(%ok2) : (i1) -> ()
    %p3 = llvm.mlir.addressof @valid0 : !llvm.ptr
    %v3 = llvm.load %p3 : !llvm.ptr -> i32
    %c3 = llvm.mlir.constant(1 : i32) : i32
    %ok3 = llvm.icmp "eq" %v3, %c3 : i32
    llvm.call @require(%ok3) : (i1) -> ()
    %p4 = llvm.mlir.addressof @stages : !llvm.ptr
    %v4 = llvm.load %p4 : !llvm.ptr -> i64
    %c4 = llvm.mlir.constant(0 : i64) : i64
    %ok4 = llvm.icmp "eq" %v4, %c4 : i64
    llvm.call @require(%ok4) : (i1) -> ()
    %p5 = llvm.mlir.addressof @changed : !llvm.ptr
    %v5 = llvm.load %p5 : !llvm.ptr -> i32
    %c5 = llvm.mlir.constant(8 : i32) : i32
    %ok5 = llvm.icmp "eq" %v5, %c5 : i32
    llvm.call @require(%ok5) : (i1) -> ()
    %allowed = llvm.mlir.addressof @allowed : !llvm.ptr
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.store %one, %allowed : i32, !llvm.ptr
    llvm.call @exercise(%nba) : (i32) -> ()
    %p6 = llvm.mlir.addressof @value : !llvm.ptr
    %v6 = llvm.load %p6 : !llvm.ptr -> i16
    %c6 = llvm.mlir.constant(42240 : i16) : i16
    %ok6 = llvm.icmp "eq" %v6, %c6 : i16
    llvm.call @require(%ok6) : (i1) -> ()
    %p7 = llvm.mlir.addressof @unknown : !llvm.ptr
    %v7 = llvm.load %p7 : !llvm.ptr -> i16
    %c7 = llvm.mlir.constant(0 : i16) : i16
    %ok7 = llvm.icmp "eq" %v7, %c7 : i16
    llvm.call @require(%ok7) : (i1) -> ()
    %p8 = llvm.mlir.addressof @dirty : !llvm.ptr
    %v8 = llvm.load %p8 : !llvm.ptr -> i64
    %c8 = llvm.mlir.constant(4 : i64) : i64
    %ok8 = llvm.icmp "eq" %v8, %c8 : i64
    llvm.call @require(%ok8) : (i1) -> ()
    %p9 = llvm.mlir.addressof @summary : !llvm.ptr
    %v9 = llvm.load %p9 : !llvm.ptr -> i64
    %c9 = llvm.mlir.constant(1 : i64) : i64
    %ok9 = llvm.icmp "eq" %v9, %c9 : i64
    llvm.call @require(%ok9) : (i1) -> ()
    %p10 = llvm.mlir.addressof @ingress : !llvm.ptr
    %v10 = llvm.load %p10 : !llvm.ptr -> i64
    %c10 = llvm.mlir.constant(3 : i64) : i64
    %ok10 = llvm.icmp "eq" %v10, %c10 : i64
    llvm.call @require(%ok10) : (i1) -> ()
    %p11 = llvm.mlir.addressof @calls : !llvm.ptr
    %v11 = llvm.load %p11 : !llvm.ptr -> i64
    %c11 = llvm.mlir.constant(2 : i64) : i64
    %ok11 = llvm.icmp "eq" %v11, %c11 : i64
    llvm.call @require(%ok11) : (i1) -> ()
    %p12 = llvm.mlir.addressof @stages : !llvm.ptr
    %v12 = llvm.load %p12 : !llvm.ptr -> i64
    %c12 = llvm.mlir.constant(3 : i64) : i64
    %ok12 = llvm.icmp "eq" %v12, %c12 : i64
    llvm.call @require(%ok12) : (i1) -> ()
    %p13 = llvm.mlir.addressof @commits : !llvm.ptr
    %v13 = llvm.load %p13 : !llvm.ptr -> i64
    %c13 = llvm.mlir.constant(3 : i64) : i64
    %ok13 = llvm.icmp "eq" %v13, %c13 : i64
    llvm.call @require(%ok13) : (i1) -> ()
    %p14 = llvm.mlir.addressof @changed : !llvm.ptr
    %v14 = llvm.load %p14 : !llvm.ptr -> i32
    %c14 = llvm.mlir.constant(9 : i32) : i32
    %ok14 = llvm.icmp "eq" %v14, %c14 : i32
    llvm.call @require(%ok14) : (i1) -> ()
    %p15 = llvm.mlir.addressof @valid0 : !llvm.ptr
    %v15 = llvm.load %p15 : !llvm.ptr -> i32
    %c15 = llvm.mlir.constant(0 : i32) : i32
    %ok15 = llvm.icmp "eq" %v15, %c15 : i32
    llvm.call @require(%ok15) : (i1) -> ()
    %p16 = llvm.mlir.addressof @valid1 : !llvm.ptr
    %v16 = llvm.load %p16 : !llvm.ptr -> i32
    %c16 = llvm.mlir.constant(0 : i32) : i32
    %ok16 = llvm.icmp "eq" %v16, %c16 : i32
    llvm.call @require(%ok16) : (i1) -> ()
    %p17 = llvm.mlir.addressof @valid3 : !llvm.ptr
    %v17 = llvm.load %p17 : !llvm.ptr -> i32
    %c17 = llvm.mlir.constant(1 : i32) : i32
    %ok17 = llvm.icmp "eq" %v17, %c17 : i32
    llvm.call @require(%ok17) : (i1) -> ()
    llvm.call @exercise(%later) : (i32) -> ()
    %p18 = llvm.mlir.addressof @value : !llvm.ptr
    %v18 = llvm.load %p18 : !llvm.ptr -> i16
    %c18 = llvm.mlir.constant(44800 : i16) : i16
    %ok18 = llvm.icmp "eq" %v18, %c18 : i16
    llvm.call @require(%ok18) : (i1) -> ()
    %p19 = llvm.mlir.addressof @unknown : !llvm.ptr
    %v19 = llvm.load %p19 : !llvm.ptr -> i16
    %c19 = llvm.mlir.constant(0 : i16) : i16
    %ok19 = llvm.icmp "eq" %v19, %c19 : i16
    llvm.call @require(%ok19) : (i1) -> ()
    %p20 = llvm.mlir.addressof @dirty : !llvm.ptr
    %v20 = llvm.load %p20 : !llvm.ptr -> i64
    %c20 = llvm.mlir.constant(0 : i64) : i64
    %ok20 = llvm.icmp "eq" %v20, %c20 : i64
    llvm.call @require(%ok20) : (i1) -> ()
    %p21 = llvm.mlir.addressof @summary : !llvm.ptr
    %v21 = llvm.load %p21 : !llvm.ptr -> i64
    %c21 = llvm.mlir.constant(0 : i64) : i64
    %ok21 = llvm.icmp "eq" %v21, %c21 : i64
    llvm.call @require(%ok21) : (i1) -> ()
    %p22 = llvm.mlir.addressof @ingress : !llvm.ptr
    %v22 = llvm.load %p22 : !llvm.ptr -> i64
    %c22 = llvm.mlir.constant(7 : i64) : i64
    %ok22 = llvm.icmp "eq" %v22, %c22 : i64
    llvm.call @require(%ok22) : (i1) -> ()
    %p23 = llvm.mlir.addressof @calls : !llvm.ptr
    %v23 = llvm.load %p23 : !llvm.ptr -> i64
    %c23 = llvm.mlir.constant(2 : i64) : i64
    %ok23 = llvm.icmp "eq" %v23, %c23 : i64
    llvm.call @require(%ok23) : (i1) -> ()
    %p24 = llvm.mlir.addressof @stages : !llvm.ptr
    %v24 = llvm.load %p24 : !llvm.ptr -> i64
    %c24 = llvm.mlir.constant(4 : i64) : i64
    %ok24 = llvm.icmp "eq" %v24, %c24 : i64
    llvm.call @require(%ok24) : (i1) -> ()
    %p25 = llvm.mlir.addressof @commits : !llvm.ptr
    %v25 = llvm.load %p25 : !llvm.ptr -> i64
    %c25 = llvm.mlir.constant(4 : i64) : i64
    %ok25 = llvm.icmp "eq" %v25, %c25 : i64
    llvm.call @require(%ok25) : (i1) -> ()
    %p26 = llvm.mlir.addressof @valid3 : !llvm.ptr
    %v26 = llvm.load %p26 : !llvm.ptr -> i32
    %c26 = llvm.mlir.constant(1 : i32) : i32
    %ok26 = llvm.icmp "eq" %v26, %c26 : i32
    llvm.call @require(%ok26) : (i1) -> ()
    %error = llvm.mlir.addressof @error : !llvm.ptr
    %code = llvm.load %error : !llvm.ptr -> i32
    llvm.return %code : i32
  }
}
