// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @classes {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"

    obelisk_sim.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = true, is_interface = false
    }
    obelisk_sim.class.field @Base_value of @Base at 0 : i64 {
      is_static = false, is_weak = false
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Derived>
      %base = obelisk_sim.class.cast %object :
          !obelisk_sim.class_handle<@Derived> to
          !obelisk_sim.class_handle<@Base>
      %field = obelisk_sim.class.field_ref %base[@Base_value] :
          !obelisk_sim.class_handle<@Base> ->
          !obelisk_sim.managed_ref<i64, @Base>
      %value = arith.constant 42 : i64
      obelisk_sim.managed.store %value to %field :
          i64, !obelisk_sim.managed_ref<i64, @Base>
      %loaded = obelisk_sim.managed.load %field :
          !obelisk_sim.managed_ref<i64, @Base> -> i64
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// CHECK: obelisk_sim.class.field @Base_value of @Base at 0 offset 8 : i64
// CHECK: obelisk.bytecode.function = 0 : i32
