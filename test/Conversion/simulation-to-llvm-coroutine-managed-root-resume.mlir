// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @managed_root_resume {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.boundary"
    obelisk_sim.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Object>
      cf.br ^resume(%object : !obelisk_sim.class_handle<@Object>)
    ^resume(%live: !obelisk_sim.class_handle<@Object>):
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %is_object = obelisk_sim.class.is_instance %live is @Object :
          !obelisk_sim.class_handle<@Object>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(
          %live : !obelisk_sim.class_handle<@Object>)
    }

    obelisk_sim.func @boundary(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %activation = obelisk_sim.control.enter 1
      obelisk_sim.control.boundary %activation resume ^resume body ^body
    ^body:
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Object>
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %is_object = obelisk_sim.class.is_instance %object is @Object :
          !obelisk_sim.class_handle<@Object>
      obelisk_sim.return
    ^resume:
      obelisk_sim.return
    }
  }
}

// The normal entry path and the resume shim converge on the same semantic
// continuation. Only the shim may reacquire the activation's managed roots;
// otherwise initial execution pushes the same range record twice.
// CHECK-LABEL: llvm.func @root.__obelisk_coro_ramp(
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// CHECK: llvm.br ^[[CONT:bb[0-9]+]]
// CHECK: ^[[CONT]]({{.*}}):
// CHECK-NOT: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// CHECK: llvm.call @obelisk_rt_v1_gc_safepoint

// Registering a named-block boundary does not yield on its body edge. Roots
// must remain active until a real suspension or process return.
// CHECK-LABEL: llvm.func @boundary.__obelisk_coro_ramp(
// CHECK: llvm.call @obelisk_rt_v1_gc_managed_root_range_push
// CHECK-NOT: llvm.call @obelisk_rt_v1_gc_managed_root_range_pop
// CHECK: llvm.call @obelisk_rt_v1_control_boundary
