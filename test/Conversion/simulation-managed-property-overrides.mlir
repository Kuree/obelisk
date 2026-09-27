// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @managed_property_overrides {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    simulation.class.decl @Holder id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Holder_value of @Holder at 0 : i64 {
      is_static = false, is_weak = false
    }

    simulation.func @process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %holder = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Holder>
      %field = simulation.class.field_ref %holder[@Holder_value] :
          !simulation.class_handle<@Holder> ->
          !simulation.managed_ref<i64, @Holder>
      %value = arith.constant 42 : i64
      simulation.override %field = %value assign true :
          !simulation.managed_ref<i64, @Holder>, i64
      %owner = simulation.process.current
      simulation.dynamic_override %field = %value owner %owner
          assign false claim true :
          !simulation.managed_ref<i64, @Holder>, i64
      simulation.dynamic_override %field = %value owner %owner
          assign false claim false :
          !simulation.managed_ref<i64, @Holder>, i64
      simulation.release_override %field assign false :
          !simulation.managed_ref<i64, @Holder>
      simulation.release_override %field assign true :
          !simulation.managed_ref<i64, @Holder>
      simulation.return
    }
  }
}

// LLVM-LABEL: llvm.func @process(
// LLVM: llvm.call @obelisk_rt_v1_object_override
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_object_override
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_object_release_override
// LLVM-NOT: simulation.override
// LLVM-NOT: simulation.dynamic_override
// LLVM-NOT: simulation.release_override

// BYTECODE: intrinsic {{.*}}id=0x00010461 inputs=5 outputs=0 flags=0
// BYTECODE: intrinsic {{.*}}id=0x00010462 inputs=3 outputs=0 flags=0
// BYTECODE-COUNT-3: site {{.*}}id=0x00010461 inputs=
// BYTECODE-COUNT-2: site {{.*}}id=0x00010462 inputs=
