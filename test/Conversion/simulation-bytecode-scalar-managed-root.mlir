// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @scalar_managed_root {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"

    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Holder_value of @Holder at 0 :
        !obelisk_sim.class_handle<@Node> {
      is_static = false, is_weak = false
    }

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %holder = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Holder>
      %node = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Node>
      %field = obelisk_sim.class.field_ref %holder[@Holder_value] :
          !obelisk_sim.class_handle<@Holder> ->
          !obelisk_sim.managed_ref<!obelisk_sim.class_handle<@Node>, @Holder>
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      obelisk_sim.managed.store %node to %field :
          !obelisk_sim.class_handle<@Node>,
          !obelisk_sim.managed_ref<!obelisk_sim.class_handle<@Node>, @Holder>
      obelisk_sim.return
    }
  }
}

// ManagedRef and class-handle registers are roots in the bytecode frame
// itself. They must not acquire aggregate extraction shadows.
// CHECK-NOT: id=0x00010411
