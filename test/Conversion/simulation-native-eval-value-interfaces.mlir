// RUN: obelisk-opt %s --test-obelisk-unroll-loops --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --test-obelisk-unroll-loops --obelisk-materialize-native-eval-groups | diff %t.mlir -

// The existing growth budget keeps these helpers outlined (each does 300
// additions once its loop is unrolled). Their selected packed lanes cross
// the call as values; observers still publish/reacquire.
// CHECK-LABEL: llvm.func internal @lane_write.__obelisk_eval_values(
// CHECK-SAME: i16
// CHECK-SAME: schedule.eval.value_lanes = array<i64: 0, 0, 2, 1, 1, 0, 2, 1>
// CHECK-NOT: llvm.load {{.*}} -> i16
// CHECK-NOT: llvm.store {{.*}} : i16, !llvm.ptr
// CHECK: llvm.mlir.addressof @checksum
// CHECK: llvm.store
// CHECK-NOT: llvm.load {{.*}} -> i16
// CHECK: llvm.and
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @lane_write(
// CHECK: llvm.load
// CHECK: llvm.call @lane_write.__obelisk_eval_values
// CHECK: llvm.store
// CHECK-LABEL: llvm.func internal @lane_read.__obelisk_eval_values(
// CHECK-SAME: schedule.eval.value_lanes = array<i64: 1, 0, 2, 0, 0, 0, 2, 0>
// CHECK-NOT: llvm.load
// CHECK: llvm.mlir.addressof @checksum
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @lane_read(
// CHECK: llvm.call @lane_read.__obelisk_eval_values
// CHECK-LABEL: llvm.func @lane_run(
// CHECK-SAME: schedule.eval.ssa_unknown_ranges = 1 : i64
// CHECK-SAME: schedule.eval.ssa_value_ranges = 1 : i64
// CHECK: llvm.load
// CHECK: llvm.load
// CHECK: llvm.call @lane_write.__obelisk_eval_values
// CHECK-NOT: llvm.load
// CHECK-NOT: llvm.store
// CHECK: llvm.call @lane_read.__obelisk_eval_values
// CHECK: llvm.store
// CHECK: llvm.store
// CHECK: llvm.call @observer
// CHECK: llvm.load
// CHECK: llvm.load
// CHECK: llvm.call @lane_read.__obelisk_eval_values
// ../Runtime/simulation-native-eval-value-interfaces.test executes this module
// against its C ABI harness.

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.native.max_inline_ops = 256 : i64} {
  llvm.mlir.global @__obelisk_state_value(dense<[52, 18, 77]> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global @__obelisk_state_unknown(dense<0> : tensor<3xi8>) : !llvm.array<3 x i8>
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
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %old = llvm.load %v {alignment = 1 : i64} : !llvm.ptr -> i16
    %oldx = llvm.load %x {alignment = 1 : i64} : !llvm.ptr -> i16
    %ctx = llvm.mlir.addressof @context : !llvm.ptr
    %context = llvm.load %ctx : !llvm.ptr -> !llvm.ptr
    %seen = llvm.mlir.addressof @seen_context : !llvm.ptr
    llvm.store %context, %seen : !llvm.ptr, !llvm.ptr
    %one = llvm.mlir.constant(1 : i16) : i16
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c300 = arith.constant 300 : index
    %work299 = scf.for %i = %c0 to %c300 step %c1 iter_args(%work = %old) -> (i16) {
      %next = llvm.add %work, %one : i16
      scf.yield %next : i16
    }
    %checksum = llvm.mlir.addressof @checksum : !llvm.ptr
    llvm.store %work299, %checksum : i16, !llvm.ptr
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
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %current = llvm.load %v {alignment = 1 : i64} : !llvm.ptr -> i16
    %current_x = llvm.load %x {alignment = 1 : i64} : !llvm.ptr -> i16
    %one = llvm.mlir.constant(1 : i16) : i16
    %c0 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %c300 = arith.constant 300 : index
    %work299 = scf.for %i = %c0 to %c300 step %c1 iter_args(%work = %current) -> (i16) {
      %next = llvm.add %work, %one : i16
      scf.yield %next : i16
    }
    %checksum = llvm.mlir.addressof @checksum : !llvm.ptr
    llvm.store %work299, %checksum : i16, !llvm.ptr
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
