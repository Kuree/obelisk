// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=LE < %t.mlir
// RUN: sed 's/"e-m:e-p/"E-m:e-p/' %s | obelisk-opt --obelisk-materialize-native-eval-groups | FileCheck %s --check-prefix=BE

// Runtime behavior is checked in ../Runtime/simulation-native-group-dataflow-alias.test.

// Literal byte aliases share one SSA slot. Narrow writes preserve all other
// value/mask bytes; later overlapping reads observe the updated slot. Distinct
// snapshot/output ranges stay distinct. Byte order comes from the target.
// CHECK-LABEL: llvm.func @group()
// CHECK-SAME: obelisk.eval.coalesced_slots = 6 : i64
// CHECK-SAME: obelisk.eval.dataflow_fallback = @group.fallback
// CHECK-SAME: obelisk.eval.dataflow_slots = 9 : i64
// CHECK-SAME: obelisk.eval.predicated_dataflow
// CHECK-NOT: llvm.alloca
// CHECK: llvm.return
// LE-DAG: llvm.mlir.constant(8 : i32)
// LE-DAG: llvm.mlir.constant(-65281 : i32)
// LE-DAG: llvm.mlir.constant(-65536 : i32)
// BE-LABEL: llvm.func @group()
// BE-SAME: obelisk.eval.coalesced_slots = 6 : i64
// BE-DAG: llvm.mlir.constant(16 : i32)
// BE-DAG: llvm.mlir.constant(-16711681 : i32)
// BE-DAG: llvm.mlir.constant(65535 : i32)
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<[17,34,51,68,0,0,0,0,0,0,0,0,1,2,3,4,0,0,0,0]> : tensor<20xi8>) : !llvm.array<20 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<[-86,-69,-52,-35,0,0,0,0,0,0,0,0]> : tensor<12xi8>) : !llvm.array<12 x i8>
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @slow(0 : i32) : i32
  llvm.func @group() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %p = llvm.mlir.addressof @slow : !llvm.ptr
    %one = llvm.mlir.constant(1 : i32) : i32
    llvm.store %one, %p : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @dataflow() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %count = llvm.mlir.constant(1 : i64) : i64
    %temporary = llvm.alloca %count x i32 : (i64) -> !llvm.ptr
    %value = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %byte = llvm.getelementptr %value[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %output = llvm.getelementptr %value[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %snapshot = llvm.getelementptr %value[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %before = llvm.load %value : !llvm.ptr -> i32
    llvm.store %before, %temporary : i32, !llvm.ptr
    %reloaded = llvm.load %temporary : !llvm.ptr -> i32
    llvm.store %reloaded, %snapshot : i32, !llvm.ptr
    %nine = llvm.mlir.constant(9 : i8) : i8
    llvm.store %nine, %byte : i8, !llvm.ptr
    %after = llvm.load %value : !llvm.ptr -> i32
    llvm.store %after, %output : i32, !llvm.ptr
    %middle = llvm.load %byte : !llvm.ptr -> i16
    llvm.store %middle, %value : i16, !llvm.ptr
    %unknown = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %unknown_byte = llvm.getelementptr %unknown[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %unknown_output = llvm.getelementptr %unknown[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %unknown_snapshot = llvm.getelementptr %unknown[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %old_mask = llvm.load %unknown : !llvm.ptr -> i32
    llvm.store %old_mask, %unknown_snapshot : i32, !llvm.ptr
    %zero = llvm.mlir.constant(0 : i8) : i8
    llvm.store %zero, %unknown_byte : i8, !llvm.ptr
    %new_mask = llvm.load %unknown : !llvm.ptr -> i32
    llvm.store %new_mask, %unknown_output : i32, !llvm.ptr
    // Three transitively overlapping windows form a slot wider than any
    // individual access; a narrow store must preserve both end bytes.
    %probe0 = llvm.getelementptr %value[12] : (!llvm.ptr) -> !llvm.ptr, i8
    %probe1 = llvm.getelementptr %value[13] : (!llvm.ptr) -> !llvm.ptr, i8
    %probe2 = llvm.getelementptr %value[14] : (!llvm.ptr) -> !llvm.ptr, i8
    %saved_probe = llvm.getelementptr %value[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %updated_probe = llvm.getelementptr %value[18] : (!llvm.ptr) -> !llvm.ptr, i8
    %p0 = llvm.load %probe0 : !llvm.ptr -> i16
    llvm.store %p0, %saved_probe : i16, !llvm.ptr
    llvm.store %p0, %probe1 : i16, !llvm.ptr
    %p2 = llvm.load %probe2 : !llvm.ptr -> i16
    llvm.store %p2, %updated_probe : i16, !llvm.ptr
    llvm.return
  }
  llvm.func @main() -> i32 {
    llvm.call @group() : () -> ()
    %value = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %output = llvm.getelementptr %value[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %snapshot = llvm.getelementptr %value[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %unknown = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %unknown_output = llvm.getelementptr %unknown[4] : (!llvm.ptr) -> !llvm.ptr, i8
    %unknown_snapshot = llvm.getelementptr %unknown[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %v = llvm.load %value : !llvm.ptr -> i32
    %o = llvm.load %output : !llvm.ptr -> i32
    %s = llvm.load %snapshot : !llvm.ptr -> i32
    %u = llvm.load %unknown : !llvm.ptr -> i32
    %uo = llvm.load %unknown_output : !llvm.ptr -> i32
    %us = llvm.load %unknown_snapshot : !llvm.ptr -> i32
    %expected_v = llvm.mlir.constant(1144206089 : i32) : i32
    %expected_o = llvm.mlir.constant(1144195345 : i32) : i32
    %expected_s = llvm.mlir.constant(1144201745 : i32) : i32
    %expected_u = llvm.mlir.constant(-573833046 : i32) : i32
    %expected_us = llvm.mlir.constant(-573785174 : i32) : i32
    %cv = llvm.icmp "eq" %v, %expected_v : i32
    %co = llvm.icmp "eq" %o, %expected_o : i32
    %cs = llvm.icmp "eq" %s, %expected_s : i32
    %cu = llvm.icmp "eq" %u, %expected_u : i32
    %cuo = llvm.icmp "eq" %uo, %expected_u : i32
    %cus = llvm.icmp "eq" %us, %expected_us : i32
    %a = llvm.and %cv, %co : i1
    %b = llvm.and %cs, %cu : i1
    %c = llvm.and %cuo, %cus : i1
    %d = llvm.and %a, %b : i1
    %ok = llvm.and %d, %c : i1
    %probe = llvm.getelementptr %value[12] : (!llvm.ptr) -> !llvm.ptr, i8
    %saved_probe = llvm.getelementptr %value[16] : (!llvm.ptr) -> !llvm.ptr, i8
    %updated_probe = llvm.getelementptr %value[18] : (!llvm.ptr) -> !llvm.ptr, i8
    %p = llvm.load %probe : !llvm.ptr -> i32
    %ps = llvm.load %saved_probe : !llvm.ptr -> i16
    %pu = llvm.load %updated_probe : !llvm.ptr -> i16
    %expected_p = llvm.mlir.constant(67240193 : i32) : i32
    %expected_ps = llvm.mlir.constant(513 : i16) : i16
    %expected_pu = llvm.mlir.constant(1026 : i16) : i16
    %cp = llvm.icmp "eq" %p, %expected_p : i32
    %cps = llvm.icmp "eq" %ps, %expected_ps : i16
    %cpu = llvm.icmp "eq" %pu, %expected_pu : i16
    %pa = llvm.and %cp, %cps : i1
    %pb = llvm.and %pa, %cpu : i1
    %all = llvm.and %ok, %pb : i1
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %status = llvm.select %all, %zero, %one : i1, i32
    llvm.return %status : i32
  }
}
