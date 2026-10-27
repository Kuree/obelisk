// RUN: talos-opt %s --talos-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=dataflow_executor --implicit-check-not=dataflow_candidate --implicit-check-not=predicated_dataflow --implicit-check-not=control_flow_dataflow < %t.mlir
// RUN: talos-opt %s '--test-talos-edit-attributes=has=schedule.eval.ranked_members set="{schedule.eval.group_proof_domain = #schedule.group_proof_dependency<domain = 0 : i64, members = [0, 1], ranges = [0, 512]>}"' --talos-materialize-native-eval-groups | FileCheck %s --check-prefix=PROOF --implicit-check-not=dataflow_executor --implicit-check-not=dataflow_candidate --implicit-check-not=predicated_dataflow --implicit-check-not=cyclic_group.fallback --implicit-check-not=volatile_group.fallback --implicit-check-not='@loop' --implicit-check-not='@volatile_read'
// CHECK: llvm.func @group()
// CHECK: llvm.return
// CHECK: llvm.func @called_candidate()
// CHECK: llvm.call @called_candidate()
// A dense predicated candidate must prove all effects and all physical ranges.
// These rejected bodies are removed without installing a speculative entry.
// With an independent group certificate the candidate keeps its CFG and
// speculates nothing, so the escaping temporary and its call are accepted
// as the certified entry; a cyclic CFG and a volatile access still are not.
// PROOF-DAG: llvm.func @group() attributes {schedule.eval.control_flow_dataflow
// PROOF-DAG: llvm.call @escape(
// PROOF-DAG: llvm.call @group.fallback()
// PROOF-DAG: llvm.func @cyclic_group() attributes {schedule.eval.group_ingress
// PROOF-DAG: llvm.func @volatile_group() attributes {schedule.eval.group_ingress
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  llvm.mlir.global internal @__talos_state_value(dense<0> : tensor<64xi8>) : !llvm.array<64 x i8>
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__talos_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  // Guard oracle for the certified-CFG run; unused without a proof domain.
  llvm.mlir.global internal @__talos_eval_group_promotion_latched_v1(dense<0> : tensor<1xi8>) : !llvm.array<1 x i8>
  llvm.func @__talos_eval_group_promotion_ready_v1(%id: i64) -> i1 {
    %yes = llvm.mlir.constant(true) : i1
    llvm.return %yes : i1
  }
  llvm.func @foreign()
  llvm.func @escape(!llvm.ptr)
  llvm.func @escaping_temporary() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %one = llvm.mlir.constant(1 : i64) : i64
    %temporary = llvm.alloca %one x i32 : (i64) -> !llvm.ptr
    llvm.call @escape(%temporary) : (!llvm.ptr) -> ()
    llvm.return
  }
  llvm.func @self_candidate() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @self_candidate} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @declaration() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready}
  llvm.func @missing_body() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @declaration} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @group() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready} {
    llvm.return
  }
  // A transient candidate with an existing caller cannot be repurposed as
  // the group's cold helper. Preserve its callable body and symbol.
  llvm.func @called_candidate() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @caller() {
    llvm.call @called_candidate() : () -> ()
    llvm.return
  }
  llvm.func @division() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    %denominator = llvm.load %base : !llvm.ptr -> i8
    %result = llvm.udiv %value, %denominator : i8
    llvm.store %result, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @remainder() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    %denominator = llvm.load %base : !llvm.ptr -> i8
    %result = llvm.urem %value, %denominator : i8
    llvm.store %result, %base : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @out_of_bounds() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %byte = llvm.getelementptr %base[63] : (!llvm.ptr) -> !llvm.ptr, i8
    %value = llvm.load %base : !llvm.ptr -> i256
    %low = llvm.trunc %value : i256 to i16
    llvm.store %low, %byte : i16, !llvm.ptr
    llvm.return
  }
  llvm.func @cyclic_group() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready} {
    llvm.return
  }
  llvm.func @loop() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @cyclic_group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %cross = llvm.getelementptr %base[31] : (!llvm.ptr) -> !llvm.ptr, i8
    %one = llvm.mlir.constant(1 : i8) : i8
    llvm.br ^body
  ^body:
    // A wide alias window does not bypass the finite-CFG proof.
    %wide = llvm.load %base : !llvm.ptr -> i256
    %low = llvm.trunc %wide : i256 to i16
    llvm.store %low, %cross : i16, !llvm.ptr
    %value = llvm.load %base : !llvm.ptr -> i8
    %next = llvm.add %value, %one : i8
    llvm.store %next, %base : i8, !llvm.ptr
    %again = llvm.icmp "ne" %next, %one : i8
    llvm.cond_br %again, ^body, ^done
  ^done:
    llvm.return
  }
  llvm.func @observer() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @opaque() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @group} {
    %one = llvm.mlir.constant(1 : i8) : i8
    %bits = llvm.mlir.constant(1 : i64) : i64
    %address = llvm.inttoptr %bits : i64 to !llvm.ptr
    llvm.store %one, %address : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @volatile_group() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready} {
    llvm.return
  }
  llvm.func @volatile_read() attributes {schedule.eval.ranked_members = array<i32: 0, 1>, schedule.eval.group_ingress = @ready, schedule.eval.dataflow_candidate = @volatile_group} {
    %base = llvm.mlir.addressof @__talos_state_value : !llvm.ptr
    %value = llvm.load volatile %base : !llvm.ptr -> i8
    llvm.store %value, %base : i8, !llvm.ptr
    llvm.return
  }
}
