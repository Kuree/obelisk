// RUN: obelisk-opt %s --obelisk-materialize-native-eval-groups -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=group_children --implicit-check-not=group_parent --implicit-check-not=dataflow_executor --implicit-check-not=dataflow_candidate < %t.mlir

// An unsuccessful refinement must retain the exact original executor. Entry
// metadata is not a license to bypass effects, replay a captured load, ignore
// an early return, or invent an owner. Even pure, speculatable freeze cannot
// be replayed: its captured choice must remain correlated across children.
// No unused child helpers survive.
// CHECK: llvm.func @foreign()
module attributes {obelisk.native.max_inline_ops = 5000 : i64} {
  llvm.mlir.global internal @ready(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.mlir.global internal @__obelisk_state_value(dense<0> : tensor<1xi8>) : !llvm.array<1 x i8>
  llvm.mlir.global internal @__obelisk_eval_promotion_pending_mask_v1(dense<0> : tensor<1xi64>) : !llvm.array<1 x i64>
  llvm.func @foreign()
  llvm.func @frozen_capture() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %state = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %undefined = llvm.mlir.undef : i8
    %captured = llvm.freeze %undefined : i8
    llvm.store %captured, %state : i8, !llvm.ptr
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.store %captured, %state : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @frozen_capture_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @frozen_capture} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %state = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %undefined = llvm.mlir.undef : i8
    %captured = llvm.freeze %undefined : i8
    llvm.store %captured, %state : i8, !llvm.ptr
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.store %captured, %state : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @all_rejected() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @all_rejected_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @all_rejected} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @duplicate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @duplicate_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @duplicate} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @missing() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @missing_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @missing} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @wrong_owner() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 2 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @wrong_owner_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @wrong_owner} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 2 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @prefix_effect() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @prefix_effect_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @prefix_effect} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.return
  }
  llvm.func @bypass() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %zero = llvm.mlir.constant(0 : i64) : i64
    %skip = llvm.icmp "eq" %r0, %zero : i64
    llvm.cond_br %skip, ^done, ^next
  ^next:
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @bypass_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @bypass} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %zero = llvm.mlir.constant(0 : i64) : i64
    %skip = llvm.icmp "eq" %r0, %zero : i64
    llvm.cond_br %skip, ^done, ^next
  ^next:
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @cross_capture() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %state = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %captured = llvm.load %state : !llvm.ptr -> i8
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.store %captured, %state : i8, !llvm.ptr
    llvm.return
  }
  llvm.func @cross_capture_candidate() attributes {obelisk.eval.ranked_members = array<i32: 0, 1>, obelisk.eval.group_ingress = @ready, obelisk.eval.dataflow_candidate = @cross_capture} {
    %ready = llvm.mlir.addressof @ready : !llvm.ptr
    %r0 = llvm.load %ready {obelisk.eval.activation_entry = 0 : i32} : !llvm.ptr -> i64
    llvm.store %r0, %ready : i64, !llvm.ptr
    llvm.call @foreign() : () -> ()
    %state = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %captured = llvm.load %state : !llvm.ptr -> i8
    %r1 = llvm.load %ready {obelisk.eval.activation_entry = 1 : i32} : !llvm.ptr -> i64
    llvm.store %r1, %ready : i64, !llvm.ptr
    llvm.store %captured, %state : i8, !llvm.ptr
    llvm.return
  }
}
