// RUN: talos-opt %s --talos-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %s --mlir-disable-threading --talos-materialize-native-eval-groups | diff %t.mlir -
// RUN: talos-opt %s '--test-talos-edit-attributes=has=schedule.eval.refresh_group_proofs remove=schedule.eval.refresh_group_proofs' --talos-materialize-native-eval-groups | FileCheck %s --check-prefix=CACHED

// Owner 65 is inactive but its cached proof is initially pending. Query it
// before entering the dataflow body. Only the group's pending bits are read;
// an actual X/Z result must take the fallback and retain pending status.
// Later knownness recovery must retry the proof without executing the inactive
// source owner (LRM 4.3, 6.3.1, 6.11.2). Runtime checks cover both words,
// the first failed proof, cached success, and unknown/known recovery.
// CACHED-LABEL: llvm.func @group()
// CACHED-NOT: llvm.call @__talos_eval_kernel_promotion_ready_v1
// CACHED-LABEL: llvm.func @group.fallback()
// CHECK-LABEL: llvm.func @group()
// CHECK-SAME: schedule.eval.predicated_dataflow
// CHECK: llvm.cond_br
// CHECK: llvm.call @group.fallback()
// CHECK: llvm.intr.cttz
// CHECK: llvm.call @__talos_eval_kernel_promotion_ready_v1
// CHECK: llvm.cond_br
// CHECK: llvm.intr.cttz
// CHECK: llvm.call @__talos_eval_kernel_promotion_ready_v1
// CHECK-LABEL: llvm.func @group.fallback()
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__talos_state_value(dense<[7, 0]> : tensor<2xi8>) : !llvm.array<2 x i8>
  llvm.mlir.global internal @__talos_state_unknown(dense<0> : tensor<2xi8>) : !llvm.array<2 x i8>
  llvm.mlir.global internal @ready(dense<[1, 0]> : tensor<2xi64>) : !llvm.array<2 x i64>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<[0, 6]> : tensor<2xi64>) : !llvm.array<2 x i64>
  llvm.mlir.global internal @calls(0 : i32) : i32
  llvm.mlir.global internal @slow(0 : i32) : i32
  llvm.mlir.global internal @last(0 : i64) : i64
  llvm.func @__talos_eval_kernel_promotion_ready_v1(%owner: i64) -> i1 {
    %calls = llvm.mlir.addressof @calls : !llvm.ptr
    %previous = llvm.load %calls : !llvm.ptr -> i32
    %one32 = llvm.mlir.constant(1 : i32) : i32
    %count = llvm.add %previous, %one32 : i32
    llvm.store %count, %calls : i32, !llvm.ptr
    %last = llvm.mlir.addressof @last : !llvm.ptr
    llvm.store %owner, %last : i64, !llvm.ptr
    %six = llvm.mlir.constant(6 : i64) : i64
    %word = llvm.lshr %owner, %six : i64
    %unknown = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %flag = llvm.getelementptr %unknown[%word] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %bits = llvm.load %flag : !llvm.ptr -> i8
    %zero8 = llvm.mlir.constant(0 : i8) : i8
    %known = llvm.icmp "eq" %bits, %zero8 : i8
    llvm.cond_br %known, ^publish, ^done
  ^publish:
    %pending = llvm.mlir.addressof @__talos_eval_promotion_pending_mask_v1 : !llvm.ptr
    %slot = llvm.getelementptr %pending[%word] : (!llvm.ptr, i64) -> !llvm.ptr, i64
    %old = llvm.load %slot : !llvm.ptr -> i64
    %sixtythree = llvm.mlir.constant(63 : i64) : i64
    %index = llvm.and %owner, %sixtythree : i64
    %one = llvm.mlir.constant(1 : i64) : i64
    %bit = llvm.shl %one, %index : i64
    %ones = llvm.mlir.constant(-1 : i64) : i64
    %keep = llvm.xor %bit, %ones : i64
    %next = llvm.and %old, %keep : i64
    llvm.store %next, %slot : i64, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return %known : i1
  }
  llvm.func @group() attributes {schedule.eval.refresh_group_proofs, schedule.eval.ranked_members = array<i32: 0, 65>, schedule.eval.group_ingress = @ready} {
    %p = llvm.mlir.addressof @slow : !llvm.ptr
    %old = llvm.load %p : !llvm.ptr -> i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %next = llvm.add %old, %one : i32
    llvm.store %next, %p : i32, !llvm.ptr
    llvm.return
  }
  llvm.func @candidate() attributes {schedule.eval.refresh_group_proofs, schedule.eval.ranked_members = array<i32: 0, 65>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %output = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %pending = llvm.load %ready : !llvm.ptr -> i64
    %zero = llvm.mlir.constant(0 : i64) : i64
    %active = llvm.icmp "ne" %pending, %zero : i64
    llvm.cond_br %active, ^run, ^done
  ^run:
    %input = llvm.load %base : !llvm.ptr -> i8
    %one = llvm.mlir.constant(1 : i8) : i8
    %next = llvm.add %input, %one : i8
    llvm.store %next, %output : i8, !llvm.ptr
    llvm.store %zero, %ready : i64, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @expect(%value: i8, %queries: i32, %fallbacks: i32, %p0: i64, %p1: i64, %owner: i64) -> i1 {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %out = llvm.getelementptr %base[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %v = llvm.load %out : !llvm.ptr -> i8
    %a = llvm.icmp "eq" %v, %value : i8
    %calls = llvm.mlir.addressof @calls : !llvm.ptr
    %c = llvm.load %calls : !llvm.ptr -> i32
    %b = llvm.icmp "eq" %c, %queries : i32
    %slow = llvm.mlir.addressof @slow : !llvm.ptr
    %n = llvm.load %slow : !llvm.ptr -> i32
    %d = llvm.icmp "eq" %n, %fallbacks : i32
    %pending = llvm.mlir.addressof @__talos_eval_promotion_pending_mask_v1 : !llvm.ptr
    %second = llvm.getelementptr %pending[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %x = llvm.load %pending : !llvm.ptr -> i64
    %y = llvm.load %second : !llvm.ptr -> i64
    %e = llvm.icmp "eq" %x, %p0 : i64
    %f = llvm.icmp "eq" %y, %p1 : i64
    %last = llvm.mlir.addressof @last : !llvm.ptr
    %l = llvm.load %last : !llvm.ptr -> i64
    %g = llvm.icmp "eq" %l, %owner : i64
    %ab = llvm.and %a, %b : i1
    %de = llvm.and %d, %e : i1
    %fg = llvm.and %f, %g : i1
    %abde = llvm.and %ab, %de : i1
    %ok = llvm.and %abde, %fg : i1
    llvm.return %ok : i1
  }
  llvm.func @main() -> i32 {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %unknown = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %unknown1 = llvm.getelementptr %unknown[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %pending0 = llvm.mlir.addressof @__talos_eval_promotion_pending_mask_v1 : !llvm.ptr
    %pending1 = llvm.getelementptr %pending0[8] : (!llvm.ptr) -> !llvm.ptr, i8
    %input0 = llvm.mlir.constant(7 : i8) : i8
    llvm.store %input0, %base : i8, !llvm.ptr
    %u00 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u00, %unknown : i8, !llvm.ptr
    %u10 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u10, %unknown1 : i8, !llvm.ptr
    %p00 = llvm.mlir.constant(0 : i64) : i64
    llvm.store %p00, %pending0 : i64, !llvm.ptr
    %p10 = llvm.mlir.constant(6 : i64) : i64
    llvm.store %p10, %pending1 : i64, !llvm.ptr
    %ready0 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %ready0, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %expected0_0 = llvm.mlir.constant(8 : i8) : i8
    %expected0_1 = llvm.mlir.constant(1 : i32) : i32
    %expected0_2 = llvm.mlir.constant(0 : i32) : i32
    %expected0_3 = llvm.mlir.constant(0 : i64) : i64
    %expected0_4 = llvm.mlir.constant(4 : i64) : i64
    %expected0_5 = llvm.mlir.constant(65 : i64) : i64
    %ok0 = llvm.call @expect(%expected0_0, %expected0_1, %expected0_2, %expected0_3, %expected0_4, %expected0_5) : (i8, i32, i32, i64, i64, i64) -> i1
    %input1 = llvm.mlir.constant(8 : i8) : i8
    llvm.store %input1, %base : i8, !llvm.ptr
    %u01 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u01, %unknown : i8, !llvm.ptr
    %u11 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u11, %unknown1 : i8, !llvm.ptr
    %p01 = llvm.mlir.constant(0 : i64) : i64
    llvm.store %p01, %pending0 : i64, !llvm.ptr
    %p11 = llvm.mlir.constant(4 : i64) : i64
    llvm.store %p11, %pending1 : i64, !llvm.ptr
    %ready1 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %ready1, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %expected1_0 = llvm.mlir.constant(9 : i8) : i8
    %expected1_1 = llvm.mlir.constant(1 : i32) : i32
    %expected1_2 = llvm.mlir.constant(0 : i32) : i32
    %expected1_3 = llvm.mlir.constant(0 : i64) : i64
    %expected1_4 = llvm.mlir.constant(4 : i64) : i64
    %expected1_5 = llvm.mlir.constant(65 : i64) : i64
    %ok1 = llvm.call @expect(%expected1_0, %expected1_1, %expected1_2, %expected1_3, %expected1_4, %expected1_5) : (i8, i32, i32, i64, i64, i64) -> i1
    %all1 = llvm.and %ok0, %ok1 : i1
    %input2 = llvm.mlir.constant(9 : i8) : i8
    llvm.store %input2, %base : i8, !llvm.ptr
    %u02 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %u02, %unknown : i8, !llvm.ptr
    %u12 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u12, %unknown1 : i8, !llvm.ptr
    %p02 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %p02, %pending0 : i64, !llvm.ptr
    %p12 = llvm.mlir.constant(6 : i64) : i64
    llvm.store %p12, %pending1 : i64, !llvm.ptr
    %ready2 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %ready2, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %expected2_0 = llvm.mlir.constant(9 : i8) : i8
    %expected2_1 = llvm.mlir.constant(2 : i32) : i32
    %expected2_2 = llvm.mlir.constant(1 : i32) : i32
    %expected2_3 = llvm.mlir.constant(1 : i64) : i64
    %expected2_4 = llvm.mlir.constant(6 : i64) : i64
    %expected2_5 = llvm.mlir.constant(0 : i64) : i64
    %ok2 = llvm.call @expect(%expected2_0, %expected2_1, %expected2_2, %expected2_3, %expected2_4, %expected2_5) : (i8, i32, i32, i64, i64, i64) -> i1
    %all2 = llvm.and %all1, %ok2 : i1
    %input3 = llvm.mlir.constant(10 : i8) : i8
    llvm.store %input3, %base : i8, !llvm.ptr
    %u03 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u03, %unknown : i8, !llvm.ptr
    %u13 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u13, %unknown1 : i8, !llvm.ptr
    %p03 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %p03, %pending0 : i64, !llvm.ptr
    %p13 = llvm.mlir.constant(6 : i64) : i64
    llvm.store %p13, %pending1 : i64, !llvm.ptr
    %ready3 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %ready3, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %expected3_0 = llvm.mlir.constant(11 : i8) : i8
    %expected3_1 = llvm.mlir.constant(4 : i32) : i32
    %expected3_2 = llvm.mlir.constant(1 : i32) : i32
    %expected3_3 = llvm.mlir.constant(0 : i64) : i64
    %expected3_4 = llvm.mlir.constant(4 : i64) : i64
    %expected3_5 = llvm.mlir.constant(65 : i64) : i64
    %ok3 = llvm.call @expect(%expected3_0, %expected3_1, %expected3_2, %expected3_3, %expected3_4, %expected3_5) : (i8, i32, i32, i64, i64, i64) -> i1
    %all3 = llvm.and %all2, %ok3 : i1
    %input4 = llvm.mlir.constant(11 : i8) : i8
    llvm.store %input4, %base : i8, !llvm.ptr
    %u04 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u04, %unknown : i8, !llvm.ptr
    %u14 = llvm.mlir.constant(1 : i8) : i8
    llvm.store %u14, %unknown1 : i8, !llvm.ptr
    %p04 = llvm.mlir.constant(0 : i64) : i64
    llvm.store %p04, %pending0 : i64, !llvm.ptr
    %p14 = llvm.mlir.constant(6 : i64) : i64
    llvm.store %p14, %pending1 : i64, !llvm.ptr
    %ready4 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %ready4, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %expected4_0 = llvm.mlir.constant(11 : i8) : i8
    %expected4_1 = llvm.mlir.constant(5 : i32) : i32
    %expected4_2 = llvm.mlir.constant(2 : i32) : i32
    %expected4_3 = llvm.mlir.constant(0 : i64) : i64
    %expected4_4 = llvm.mlir.constant(6 : i64) : i64
    %expected4_5 = llvm.mlir.constant(65 : i64) : i64
    %ok4 = llvm.call @expect(%expected4_0, %expected4_1, %expected4_2, %expected4_3, %expected4_4, %expected4_5) : (i8, i32, i32, i64, i64, i64) -> i1
    %all4 = llvm.and %all3, %ok4 : i1
    %input5 = llvm.mlir.constant(12 : i8) : i8
    llvm.store %input5, %base : i8, !llvm.ptr
    %u05 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u05, %unknown : i8, !llvm.ptr
    %u15 = llvm.mlir.constant(0 : i8) : i8
    llvm.store %u15, %unknown1 : i8, !llvm.ptr
    %p05 = llvm.mlir.constant(0 : i64) : i64
    llvm.store %p05, %pending0 : i64, !llvm.ptr
    %p15 = llvm.mlir.constant(6 : i64) : i64
    llvm.store %p15, %pending1 : i64, !llvm.ptr
    %ready5 = llvm.mlir.constant(1 : i64) : i64
    llvm.store %ready5, %ready : i64, !llvm.ptr
    llvm.call @group() : () -> ()
    %expected5_0 = llvm.mlir.constant(13 : i8) : i8
    %expected5_1 = llvm.mlir.constant(6 : i32) : i32
    %expected5_2 = llvm.mlir.constant(2 : i32) : i32
    %expected5_3 = llvm.mlir.constant(0 : i64) : i64
    %expected5_4 = llvm.mlir.constant(4 : i64) : i64
    %expected5_5 = llvm.mlir.constant(65 : i64) : i64
    %ok5 = llvm.call @expect(%expected5_0, %expected5_1, %expected5_2, %expected5_3, %expected5_4, %expected5_5) : (i8, i32, i32, i64, i64, i64) -> i1
    %all5 = llvm.and %all4, %ok5 : i1
    %zero = llvm.mlir.constant(0 : i32) : i32
    %one = llvm.mlir.constant(1 : i32) : i32
    %status = llvm.select %all5, %zero, %one : i1, i32
    llvm.return %status : i32
  }
}
