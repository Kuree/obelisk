// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --obelisk-materialize-native-eval-groups -o %t.serial
// RUN: diff %t.mlir %t.serial

// Runtime behavior is checked in ../Runtime/simulation-native-group-state-boundary.test.

// A lowering-stage fixture supplies already-certified group calls. Both
// canonical planes must be visible at the retained helper boundary, and both
// must be reacquired after its mutation. The executable checks its observations
// and the later actor's result, including an unknown-to-known transition.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<[7, 0, 0]> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global internal @__obelisk_state_unknown(dense<[0, 0, 0]> : tensor<3xi8>) : !llvm.array<3 x i8>

  llvm.func @writer() attributes {schedule.eval.infallible} {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %one = llvm.mlir.constant(1 : i8) : i8
    %all = llvm.mlir.constant(-1 : i8) : i8
    %oldv = llvm.load %v : !llvm.ptr -> i8
    %oldx = llvm.load %x : !llvm.ptr -> i8
    %newv = llvm.add %oldv, %one : i8
    %newx = llvm.xor %oldx, %all : i8
    llvm.store %newv, %v : i8, !llvm.ptr
    llvm.store %newx, %x : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @reader() attributes {schedule.eval.infallible} {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %outv = llvm.getelementptr %v[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %outx = llvm.getelementptr %x[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %v : !llvm.ptr -> i8
    %unknown = llvm.load %x : !llvm.ptr -> i8
    llvm.store %value, %outv : i8, !llvm.ptr
    llvm.store %unknown, %outx : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @after_mutation() attributes {schedule.eval.infallible} {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %outv = llvm.getelementptr %v[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %outx = llvm.getelementptr %x[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %v : !llvm.ptr -> i8
    %unknown = llvm.load %x : !llvm.ptr -> i8
    llvm.store %value, %outv : i8, !llvm.ptr
    llvm.store %unknown, %outx : i8, !llvm.ptr
    llvm.return
  }
  // This unmarked function is an opaque boundary to group materialization.
  llvm.func @mutate() -> i1 attributes {passthrough = ["noinline"]} {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %value = llvm.load %v : !llvm.ptr -> i8
    %unknown = llvm.load %x : !llvm.ptr -> i8
    %eight = llvm.mlir.constant(8 : i8) : i8
    %all = llvm.mlir.constant(-1 : i8) : i8
    %vok = llvm.icmp "eq" %value, %eight : i8
    %xok = llvm.icmp "eq" %unknown, %all : i8
    %ok = llvm.and %vok, %xok : i1
    %forty = llvm.mlir.constant(40 : i8) : i8
    %zero = llvm.mlir.constant(0 : i8) : i8
    llvm.store %forty, %v : i8, !llvm.ptr
    llvm.store %zero, %x : i8, !llvm.ptr
    llvm.return %ok : i1
  }

  // CHECK-LABEL: llvm.func @group()
  // CHECK-SAME: schedule.eval.ssa_unknown_ranges = 1 : i64
  // CHECK-SAME: schedule.eval.ssa_value_ranges = 1 : i64
  // CHECK: %[[X:.*]] = llvm.mlir.addressof @__obelisk_state_unknown
  // CHECK: %[[XP:.*]] = llvm.getelementptr %[[X]][0]
  // CHECK: llvm.load %[[XP]]{{[ {]}}
  // CHECK: %[[V:.*]] = llvm.mlir.addressof @__obelisk_state_value
  // CHECK: %[[VP:.*]] = llvm.getelementptr %[[V]][0]
  // CHECK: llvm.load %[[VP]]{{[ {]}}
  // CHECK: llvm.store {{.*}}, %[[VP]]{{[ {]}}
  // CHECK-NEXT: llvm.store {{.*}}, %[[XP]]{{[ {]}}
  // CHECK-NEXT: {{.*}}llvm.call @mutate()
  // CHECK-NEXT: {{.*}}llvm.load %[[XP]]{{[ {]}}
  // CHECK-NEXT: {{.*}}llvm.load %[[VP]]{{[ {]}}
  llvm.func @group() -> i1 attributes {schedule.eval.ranked_members = array<i32: 0, 1>} {
    llvm.call @writer() {schedule.eval.group_member = 0 : i32} : () -> ()
    llvm.call @reader() {schedule.eval.group_member = 1 : i32} : () -> ()
    %ok = llvm.call @mutate() : () -> i1
    llvm.call @after_mutation() {schedule.eval.group_member = 2 : i32} : () -> ()
    llvm.return %ok : i1
  }

  // Indirect stores may alias either plane, even when the pointer's target
  // cannot be established inside the group. The later reader must observe
  // those mutations rather than the cached results from writer.
  // CHECK-LABEL: llvm.func @indirect_group(
  // CHECK-SAME: schedule.eval.ssa_unknown_ranges = 1 : i64
  // CHECK-SAME: schedule.eval.ssa_value_ranges = 1 : i64
  llvm.func @indirect_group(%v: !llvm.ptr, %x: !llvm.ptr) attributes {schedule.eval.ranked_members = array<i32: 0>} {
    llvm.call @writer() {schedule.eval.group_member = 0 : i32} : () -> ()
    %forty = llvm.mlir.constant(40 : i8) : i8
    %zero = llvm.mlir.constant(0 : i8) : i8
    llvm.store %forty, %v : i8, !llvm.ptr
    llvm.store %zero, %x : i8, !llvm.ptr
    llvm.call @reader() {schedule.eval.group_member = 1 : i32} : () -> ()
    llvm.return
  }
  llvm.func @main() -> i32 {
    %observed = llvm.call @group() : () -> i1
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %outv = llvm.getelementptr %v[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %outx = llvm.getelementptr %x[2] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %outv : !llvm.ptr -> i8
    %unknown = llvm.load %outx : !llvm.ptr -> i8
    %forty = llvm.mlir.constant(40 : i8) : i8
    %zero = llvm.mlir.constant(0 : i8) : i8
    %vok = llvm.icmp "eq" %value, %forty : i8
    %xok = llvm.icmp "eq" %unknown, %zero : i8
    %stateok = llvm.and %vok, %xok : i1
    %boundaryok = llvm.and %observed, %stateok : i1
    llvm.call @indirect_group(%v, %x) : (!llvm.ptr, !llvm.ptr) -> ()
    %indirectv = llvm.getelementptr %v[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %indirectx = llvm.getelementptr %x[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %ivalue = llvm.load %indirectv : !llvm.ptr -> i8
    %iunknown = llvm.load %indirectx : !llvm.ptr -> i8
    %ivok = llvm.icmp "eq" %ivalue, %forty : i8
    %ixok = llvm.icmp "eq" %iunknown, %zero : i8
    %indirectok = llvm.and %ivok, %ixok : i1
    %ok = llvm.and %boundaryok, %indirectok : i1
    %pass = llvm.mlir.constant(0 : i32) : i32
    %fail = llvm.mlir.constant(1 : i32) : i32
    %result = llvm.select %ok, %pass, %fail : i1, i32
    llvm.return %result : i32
  }
}
