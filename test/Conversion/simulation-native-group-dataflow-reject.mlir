// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=dataflow_executor --implicit-check-not=dataflow_candidate --implicit-check-not=predicated_dataflow < %t.mlir
// CHECK: llvm.func @group()
// CHECK: llvm.return
// CHECK: llvm.func @called_candidate()
// CHECK: llvm.call @called_candidate()
// A candidate must prove all effects and all physical ranges. These rejected
// bodies are removed without installing a speculative group entry.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.func @foreign()
  llvm.func @escape(!llvm.ptr)
  llvm.func @escaping_temporary() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %one = llvm.mlir.constant(1 : i64) : i64
    %temporary = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    llvm.call @escape(%temporary) : (!llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @self_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @self_candidate} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @declaration() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready}
  llvm.func @missing_body() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @declaration} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @group() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    llvm.return
  }
  // A transient candidate with an existing caller cannot be repurposed as
  // the group's cold helper. Preserve its callable body and symbol.
  llvm.func @called_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @caller() {
    llvm.call @called_candidate() : () -> ()
    llvm.return
  }
  llvm.func @division() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    %denominator = llvm.load %base : !llvm.ptr -> i8
    %result = llvm.udiv %value, %denominator : i8
    llvm.store %result, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @remainder() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    %denominator = llvm.load %base : !llvm.ptr -> i8
    %result = llvm.urem %value, %denominator : i8
    llvm.store %result, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @overlap() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %byte = llvm.getelementptr %base[31] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %base : !llvm.ptr -> i256
    %low = llvm.trunc %value : i256 to i16
    llvm.store %low, %byte : i16, !llvm.ptr
    llvm.return
  }
  llvm.func @loop() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %one = llvm.mlir.constant(1 : i8) : i8
    llvm.br ^body
  ^body:
    %value = llvm.load %base : !llvm.ptr -> i8
    %next = llvm.add %value, %one : i8
    llvm.store %next, %base : i8, !llvm.ptr
    %again = llvm.icmp "ne" %next, %one : i8
    llvm.cond_br %again, ^body, ^done
  ^done:
    llvm.return
  }
  llvm.func @observer() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @opaque() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %one = llvm.mlir.constant(1 : i8) : i8
    %bits = llvm.mlir.constant(1 : i64) : i64
    %address = llvm.inttoptr %bits : i64 to !llvm.ptr
    llvm.store %one, %address : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @volatile_read() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %value = llvm.load volatile %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
}
