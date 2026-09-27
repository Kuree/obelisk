// RUN: obelisk-opt %s --obelisk-sim-instrument-managed-roots | FileCheck %s

!candidate = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Object>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "bits", type = i64, ordinal = 2, packedOffset = 0>
], isTagged = false>
!many = !simulation.unpacked_array<0 : 64 x !simulation.class_handle<@Object>>
!cross_value = !simulation.unpacked_struct<[
  #simulation.field<name = "a", type = i1, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "b", type = i1, ordinal = 1, packedOffset = 0>
]>
!cross_queue = !simulation.queue<!cross_value, 0>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @managed_roots {
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

    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "roots"
    simulation.code_unit.decl 2 in 0 function hierarchy "bulk"
    simulation.code_unit.decl 3 in 0 function hierarchy "cross_create"
    simulation.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.covergroup.decl @cg schema 1 debug "cg"

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      %candidate = simulation.union.construct %object as 0 :
          (!simulation.class_handle<@Object>) -> !candidate
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.gc.safepoint %ctx : !simulation.context
      %reloaded = simulation.union.extract %candidate[0] :
          (!candidate) -> !simulation.class_handle<@Object>
      %is_object = simulation.class.is_instance %reloaded is @Object :
          !simulation.class_handle<@Object>
      simulation.return
    }

    simulation.func @bulk(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %null = simulation.class.null : !simulation.class_handle<@Object>
      %many = simulation.aggregate.construct %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null, %null :
          (!simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>, !simulation.class_handle<@Object>) -> !many
      simulation.gc.safepoint %ctx : !simulation.context
      %reloaded = simulation.aggregate.extract %many[0] :
          (!many) -> !simulation.class_handle<@Object>
      %is_object = simulation.class.is_instance %reloaded is @Object :
          !simulation.class_handle<@Object>
      simulation.return
    }

    simulation.func @cross_create(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %queue: !cross_queue
            {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %handle = simulation.covergroup.create %ctx from @cg
        payloads [%queue] argument_count 0 formal_ids [] expression_ids [7]
        : (!cross_queue) -> !simulation.covergroup_handle<@cg>
      simulation.return
    }
  }
}

// CHECK-LABEL: simulation.func @root
// CHECK: llvm.alloca
// CHECK: llvm.alloca
// CHECK-SAME: obelisk.managed_root_range_record
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// Candidate classification is refreshed immediately before every collection;
// it is not cached once at the SSA definition.
// CHECK-NOT: simulation.class.root_bind
// CHECK: simulation.class.root_bind %[[CANDIDATE:.*]] to %[[SLOT:.*]] at 0 candidate kinds 3
// CHECK-NEXT: simulation.gc.safepoint
// CHECK: simulation.class.root_bind %[[CANDIDATE]] to %[[SLOT]] at 0 candidate kinds 3
// CHECK-NEXT: simulation.gc.safepoint
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop

// A large aggregate uses compact bulk root refreshes instead of one scalar
// dead-slot clear per root at every safepoint.
// CHECK-LABEL: simulation.func @bulk
// CHECK-COUNT-2: llvm.intr.memset
// CHECK: simulation.class.root_bind %{{.*}} to %{{.*}} at 0 exact kinds 1
// CHECK: simulation.gc.safepoint

// covergroup.create may allocate while it snapshots a queue-valued
// cross_set_expression. Keep the managed queue operand rooted across the call
// rather than relying on the current collector implementation not to run.
// CHECK-LABEL: simulation.func @cross_create
// CHECK: simulation.class.root_bind %[[QUEUE:.*]] to %{{.*}} at 0 exact kinds 4
// CHECK-NEXT: %{{.*}} = simulation.covergroup.create {{.*}}payloads[%[[QUEUE]]]
