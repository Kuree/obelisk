// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @scalar_managed_root {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"

    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Holder_value of @Holder at 0 :
        !simulation.class_handle<@Node> {
      is_static = false, is_weak = false
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %holder = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Holder>
      %node = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Node>
      %field = simulation.class.field_ref %holder[@Holder_value] :
          !simulation.class_handle<@Holder> ->
          !simulation.managed_ref<!simulation.class_handle<@Node>, @Holder>
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.managed.store %node to %field :
          !simulation.class_handle<@Node>,
          !simulation.managed_ref<!simulation.class_handle<@Node>, @Holder>
      simulation.return
    }
  }
}

// ManagedRef and class-handle registers are roots in the bytecode frame
// itself. They must not acquire aggregate extraction shadows.
// CHECK-NOT: id=0x00010411
