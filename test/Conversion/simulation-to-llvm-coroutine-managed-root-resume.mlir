// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @managed_root_resume {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.boundary"
    simulation.class.decl @Object id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      cf.br ^resume(%object : !simulation.class_handle<@Object>)
    ^resume(%live: !simulation.class_handle<@Object>):
      simulation.gc.safepoint %ctx : !simulation.context
      %is_object = simulation.class.is_instance %live is @Object :
          !simulation.class_handle<@Object>
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(
          %live : !simulation.class_handle<@Object>)
    }

    simulation.func @boundary(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %activation = simulation.control.enter 1
      simulation.control.boundary %activation resume ^resume body ^body
    ^body:
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Object>
      simulation.gc.safepoint %ctx : !simulation.context
      %is_object = simulation.class.is_instance %object is @Object :
          !simulation.class_handle<@Object>
      simulation.return
    ^resume:
      simulation.return
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
