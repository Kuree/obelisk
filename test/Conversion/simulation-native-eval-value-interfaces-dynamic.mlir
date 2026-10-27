// RUN: talos-opt %s --talos-materialize-native-eval-groups | FileCheck %s

// The bounded dynamic value-plane store intersects the fixed lane, so the
// writer's value plane stays canonical; its independent X/Z lane can travel.
// CHECK-LABEL: llvm.func internal @lane_write.__talos_eval_values(
// CHECK-SAME: schedule.eval.value_lanes = array<i64: 1, 0, 2, 1>
// CHECK: llvm.load {{.*}} -> i16
// CHECK: llvm.store {{.*}}schedule.eval.state_access_bytes

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.native.max_inline_ops = 64 : i64} {
  llvm.mlir.global @__talos_state_value(dense<[52, 18, 77]> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global @__talos_state_unknown(dense<0> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global internal @checksum(0 : i16) : i16
  llvm.mlir.global internal @context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.mlir.global internal @seen_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @observer(i16, i16) -> i1
  llvm.func @lane_write(%take: i1 {simulation.capture_kind = 1 : i32}) attributes {schedule.eval.raw_captures, passthrough = ["noinline"]} {
    %v = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %old = llvm.load %v {alignment = 1 : i64} : !llvm.ptr -> i16
    %oldx = llvm.load %x {alignment = 1 : i64} : !llvm.ptr -> i16
    %ctx = llvm.mlir.addressof @context : !llvm.ptr
    %context = llvm.load %ctx : !llvm.ptr -> !llvm.ptr
    %seen = llvm.mlir.addressof @seen_context : !llvm.ptr
    llvm.store %context, %seen : !llvm.ptr, !llvm.ptr
    %one = llvm.mlir.constant(1 : i16) : i16
    %index = llvm.zext %take : i1 to i64
    %dynamic = llvm.getelementptr %v[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %dynamic_byte = llvm.mlir.constant(0 : i8) : i8
    llvm.store %dynamic_byte, %dynamic {schedule.eval.state_access_bytes = array<i64: 0, 2>} : i8, !llvm.ptr
    %work0 = llvm.add %old, %one : i16
    %work1 = llvm.add %work0, %one : i16
    %work2 = llvm.add %work1, %one : i16
    %work3 = llvm.add %work2, %one : i16
    %work4 = llvm.add %work3, %one : i16
    %work5 = llvm.add %work4, %one : i16
    %work6 = llvm.add %work5, %one : i16
    %work7 = llvm.add %work6, %one : i16
    %work8 = llvm.add %work7, %one : i16
    %work9 = llvm.add %work8, %one : i16
    %work10 = llvm.add %work9, %one : i16
    %work11 = llvm.add %work10, %one : i16
    %work12 = llvm.add %work11, %one : i16
    %work13 = llvm.add %work12, %one : i16
    %work14 = llvm.add %work13, %one : i16
    %work15 = llvm.add %work14, %one : i16
    %work16 = llvm.add %work15, %one : i16
    %work17 = llvm.add %work16, %one : i16
    %work18 = llvm.add %work17, %one : i16
    %work19 = llvm.add %work18, %one : i16
    %work20 = llvm.add %work19, %one : i16
    %work21 = llvm.add %work20, %one : i16
    %work22 = llvm.add %work21, %one : i16
    %work23 = llvm.add %work22, %one : i16
    %work24 = llvm.add %work23, %one : i16
    %work25 = llvm.add %work24, %one : i16
    %work26 = llvm.add %work25, %one : i16
    %work27 = llvm.add %work26, %one : i16
    %work28 = llvm.add %work27, %one : i16
    %work29 = llvm.add %work28, %one : i16
    %work30 = llvm.add %work29, %one : i16
    %work31 = llvm.add %work30, %one : i16
    %work32 = llvm.add %work31, %one : i16
    %work33 = llvm.add %work32, %one : i16
    %work34 = llvm.add %work33, %one : i16
    %work35 = llvm.add %work34, %one : i16
    %work36 = llvm.add %work35, %one : i16
    %work37 = llvm.add %work36, %one : i16
    %work38 = llvm.add %work37, %one : i16
    %work39 = llvm.add %work38, %one : i16
    %work40 = llvm.add %work39, %one : i16
    %work41 = llvm.add %work40, %one : i16
    %work42 = llvm.add %work41, %one : i16
    %work43 = llvm.add %work42, %one : i16
    %work44 = llvm.add %work43, %one : i16
    %work45 = llvm.add %work44, %one : i16
    %work46 = llvm.add %work45, %one : i16
    %work47 = llvm.add %work46, %one : i16
    %work48 = llvm.add %work47, %one : i16
    %work49 = llvm.add %work48, %one : i16
    %work50 = llvm.add %work49, %one : i16
    %work51 = llvm.add %work50, %one : i16
    %work52 = llvm.add %work51, %one : i16
    %work53 = llvm.add %work52, %one : i16
    %work54 = llvm.add %work53, %one : i16
    %work55 = llvm.add %work54, %one : i16
    %work56 = llvm.add %work55, %one : i16
    %work57 = llvm.add %work56, %one : i16
    %work58 = llvm.add %work57, %one : i16
    %work59 = llvm.add %work58, %one : i16
    %work60 = llvm.add %work59, %one : i16
    %work61 = llvm.add %work60, %one : i16
    %work62 = llvm.add %work61, %one : i16
    %work63 = llvm.add %work62, %one : i16
    %work64 = llvm.add %work63, %one : i16
    %work65 = llvm.add %work64, %one : i16
    %work66 = llvm.add %work65, %one : i16
    %work67 = llvm.add %work66, %one : i16
    %work68 = llvm.add %work67, %one : i16
    %work69 = llvm.add %work68, %one : i16
    %checksum = llvm.mlir.addressof @checksum : !llvm.ptr
    llvm.store %work69, %checksum : i16, !llvm.ptr
    llvm.cond_br %take, ^update, ^done
  ^update:
    %keep = llvm.mlir.constant(-16 : i16) : i16
    %low = llvm.mlir.constant(10 : i16) : i16
    %clear = llvm.and %old, %keep : i16
    %next = llvm.or %clear, %low : i16
    llvm.store %next, %v {alignment = 1 : i64} : i16, !llvm.ptr
    %high = llvm.getelementptr %v[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %byte = llvm.mlir.constant(171 : i8) : i8
    llvm.store %byte, %high : i8, !llvm.ptr
    %xz = llvm.mlir.constant(-32768 : i16) : i16
    %newx = llvm.or %oldx, %xz : i16
    llvm.store %newx, %x {alignment = 1 : i64} : i16, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @lane_read(%expected: i16 {simulation.capture_kind = 1 : i32}, %expected_x: i16) -> i1 attributes {schedule.eval.infallible, passthrough = ["noinline"]} {
    %v = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__talos_state_unknown : !llvm.ptr
    %current = llvm.load %v {alignment = 1 : i64} : !llvm.ptr -> i16
    %current_x = llvm.load %x {alignment = 1 : i64} : !llvm.ptr -> i16
    %one = llvm.mlir.constant(1 : i16) : i16
    %work0 = llvm.add %current, %one : i16
    %work1 = llvm.add %work0, %one : i16
    %work2 = llvm.add %work1, %one : i16
    %work3 = llvm.add %work2, %one : i16
    %work4 = llvm.add %work3, %one : i16
    %work5 = llvm.add %work4, %one : i16
    %work6 = llvm.add %work5, %one : i16
    %work7 = llvm.add %work6, %one : i16
    %work8 = llvm.add %work7, %one : i16
    %work9 = llvm.add %work8, %one : i16
    %work10 = llvm.add %work9, %one : i16
    %work11 = llvm.add %work10, %one : i16
    %work12 = llvm.add %work11, %one : i16
    %work13 = llvm.add %work12, %one : i16
    %work14 = llvm.add %work13, %one : i16
    %work15 = llvm.add %work14, %one : i16
    %work16 = llvm.add %work15, %one : i16
    %work17 = llvm.add %work16, %one : i16
    %work18 = llvm.add %work17, %one : i16
    %work19 = llvm.add %work18, %one : i16
    %work20 = llvm.add %work19, %one : i16
    %work21 = llvm.add %work20, %one : i16
    %work22 = llvm.add %work21, %one : i16
    %work23 = llvm.add %work22, %one : i16
    %work24 = llvm.add %work23, %one : i16
    %work25 = llvm.add %work24, %one : i16
    %work26 = llvm.add %work25, %one : i16
    %work27 = llvm.add %work26, %one : i16
    %work28 = llvm.add %work27, %one : i16
    %work29 = llvm.add %work28, %one : i16
    %work30 = llvm.add %work29, %one : i16
    %work31 = llvm.add %work30, %one : i16
    %work32 = llvm.add %work31, %one : i16
    %work33 = llvm.add %work32, %one : i16
    %work34 = llvm.add %work33, %one : i16
    %work35 = llvm.add %work34, %one : i16
    %work36 = llvm.add %work35, %one : i16
    %work37 = llvm.add %work36, %one : i16
    %work38 = llvm.add %work37, %one : i16
    %work39 = llvm.add %work38, %one : i16
    %work40 = llvm.add %work39, %one : i16
    %work41 = llvm.add %work40, %one : i16
    %work42 = llvm.add %work41, %one : i16
    %work43 = llvm.add %work42, %one : i16
    %work44 = llvm.add %work43, %one : i16
    %work45 = llvm.add %work44, %one : i16
    %work46 = llvm.add %work45, %one : i16
    %work47 = llvm.add %work46, %one : i16
    %work48 = llvm.add %work47, %one : i16
    %work49 = llvm.add %work48, %one : i16
    %work50 = llvm.add %work49, %one : i16
    %work51 = llvm.add %work50, %one : i16
    %work52 = llvm.add %work51, %one : i16
    %work53 = llvm.add %work52, %one : i16
    %work54 = llvm.add %work53, %one : i16
    %work55 = llvm.add %work54, %one : i16
    %work56 = llvm.add %work55, %one : i16
    %work57 = llvm.add %work56, %one : i16
    %work58 = llvm.add %work57, %one : i16
    %work59 = llvm.add %work58, %one : i16
    %work60 = llvm.add %work59, %one : i16
    %work61 = llvm.add %work60, %one : i16
    %work62 = llvm.add %work61, %one : i16
    %work63 = llvm.add %work62, %one : i16
    %work64 = llvm.add %work63, %one : i16
    %work65 = llvm.add %work64, %one : i16
    %work66 = llvm.add %work65, %one : i16
    %work67 = llvm.add %work66, %one : i16
    %work68 = llvm.add %work67, %one : i16
    %work69 = llvm.add %work68, %one : i16
    %checksum = llvm.mlir.addressof @checksum : !llvm.ptr
    llvm.store %work69, %checksum : i16, !llvm.ptr
    %okv = llvm.icmp "eq" %current, %expected : i16
    %okx = llvm.icmp "eq" %current_x, %expected_x : i16
    %ok = llvm.and %okv, %okx : i1
    llvm.return %ok : i1
  }
  llvm.func @lane_run(%take: i1) -> i1 attributes {schedule.eval.ranked_members = array<i32: 0, 1, 2>} {
    llvm.call @lane_write(%take) {arg_attrs = [{llvm.noundef}], schedule.eval.group_member = 0 : i32} : (i1) -> ()
    %old = llvm.mlir.constant(4660 : i16) : i16
    %new = llvm.mlir.constant(-21702 : i16) : i16
    %zero = llvm.mlir.constant(0 : i16) : i16
    %unknown = llvm.mlir.constant(-32768 : i16) : i16
    %expected = llvm.select %take, %new, %old : i1, i16
    %expected_x = llvm.select %take, %unknown, %zero : i1, i16
    %first = llvm.call @lane_read(%expected, %expected_x) {schedule.eval.group_member = 1 : i32} : (i16, i16) -> i1
    %seen = llvm.call @observer(%expected, %expected_x) : (i16, i16) -> i1
    %mutated = llvm.mlir.constant(-275 : i16) : i16
    %mutated_x = llvm.mlir.constant(192 : i16) : i16
    %last = llvm.call @lane_read(%mutated, %mutated_x) {schedule.eval.group_member = 2 : i32} : (i16, i16) -> i1
    %reads = llvm.and %first, %last : i1
    %all = llvm.and %reads, %seen : i1
    llvm.return %all : i1
  }
}
