// RUN: obelisk-opt %s --obelisk-sim-instrument-managed-roots | FileCheck %s

module {
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
    obelisk_sim.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Object>
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %is_object = obelisk_sim.class.is_instance %object is @Object :
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
// CHECK: obelisk_sim.class.root_bind
// CHECK: obelisk_sim.gc.safepoint
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop
