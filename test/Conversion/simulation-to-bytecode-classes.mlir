// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @classes {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.field @Base_value of @Base at 0 : i64 {
      is_static = false, is_weak = false
    }

    simulation.func @process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Derived>
      %base = simulation.class.cast %object :
          !simulation.class_handle<@Derived> to
          !simulation.class_handle<@Base>
      %field = simulation.class.field_ref %base[@Base_value] :
          !simulation.class_handle<@Base> ->
          !simulation.managed_ref<i64, @Base>
      %value = arith.constant 42 : i64
      simulation.managed.store %value to %field :
          i64, !simulation.managed_ref<i64, @Base>
      %loaded = simulation.managed.load %field :
          !simulation.managed_ref<i64, @Base> -> i64
      simulation.return
    }
  }
}

// CHECK: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// CHECK: simulation.class.field @Base_value of @Base at 0 offset 8 : i64
// CHECK: obelisk.bytecode.function = 0 : i32
