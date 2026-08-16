// RUN: obelisk-opt %s --obelisk-sim-instrument-managed-roots | FileCheck %s

!candidate = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Object>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i64, ordinal = 2, packedOffset = 0>
], isTagged = false>
!many = !obelisk_sim.unpacked_array<0 : 64 x !obelisk_sim.class_handle<@Object>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @managed_roots {
    llvm.mlir.global internal @__obelisk_current_context() : !llvm.ptr {
      %null = llvm.mlir.zero : !llvm.ptr
      llvm.return %null : !llvm.ptr
    }
    llvm.func @obelisk_rt_v1_gc_current_lane(!llvm.ptr) -> !llvm.ptr
    llvm.func @obelisk_rt_v1_gc_managed_root_range_push(
        !llvm.ptr, !llvm.ptr, !llvm.ptr, i64) -> i32
    llvm.func @obelisk_rt_v1_gc_managed_root_range_pop(
        !llvm.ptr, !llvm.ptr) -> i32
    llvm.func @obelisk_rt_v1_scheduler_fail(!llvm.ptr, i32)

    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "roots"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "bulk"
    obelisk_sim.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Object>
      %candidate = obelisk_sim.union.construct %object as 0 :
          (!obelisk_sim.class_handle<@Object>) -> !candidate
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %reloaded = obelisk_sim.union.extract %candidate[0] :
          (!candidate) -> !obelisk_sim.class_handle<@Object>
      %is_object = obelisk_sim.class.is_instance %reloaded is @Object :
          !obelisk_sim.class_handle<@Object>
      obelisk_sim.return
    }

    obelisk_sim.func @bulk(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %null = obelisk_sim.class.null : !obelisk_sim.class_handle<@Object>
      %many = obelisk_sim.aggregate.construct %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null :
          (!obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>, !obelisk_sim.class_handle<@Object>) -> !many
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %reloaded = obelisk_sim.aggregate.extract %many[0] :
          (!many) -> !obelisk_sim.class_handle<@Object>
      %is_object = obelisk_sim.class.is_instance %reloaded is @Object :
          !obelisk_sim.class_handle<@Object>
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @root
// CHECK: llvm.alloca
// CHECK: llvm.alloca
// CHECK-SAME: obelisk.managed_root_range_record
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// Candidate classification is refreshed immediately before every collection;
// it is not cached once at the SSA definition.
// CHECK-NOT: obelisk_sim.class.root_bind
// CHECK: obelisk_sim.class.root_bind %[[CANDIDATE:.*]] to %[[SLOT:.*]] at 0 candidate kinds 3
// CHECK-NEXT: obelisk_sim.gc.safepoint
// CHECK: obelisk_sim.class.root_bind %[[CANDIDATE]] to %[[SLOT]] at 0 candidate kinds 3
// CHECK-NEXT: obelisk_sim.gc.safepoint
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop

// A large aggregate uses compact bulk root refreshes instead of one scalar
// dead-slot clear per root at every safepoint.
// CHECK-LABEL: obelisk_sim.func @bulk
// CHECK-COUNT-2: llvm.intr.memset
// CHECK: obelisk_sim.class.root_bind %{{.*}} to %{{.*}} at 0 exact kinds 1
// CHECK: obelisk_sim.gc.safepoint
