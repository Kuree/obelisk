// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @managed_property_overrides {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"

    obelisk_sim.class.decl @Holder id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Holder_value of @Holder at 0 : i64 {
      is_static = false, is_weak = false
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %holder = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Holder>
      %field = obelisk_sim.class.field_ref %holder[@Holder_value] :
          !obelisk_sim.class_handle<@Holder> ->
          !obelisk_sim.managed_ref<i64, @Holder>
      %value = arith.constant 42 : i64
      obelisk_sim.override %field = %value assign true :
          !obelisk_sim.managed_ref<i64, @Holder>, i64
      %owner = obelisk_sim.process.current
      obelisk_sim.dynamic_override %field = %value owner %owner
          assign false claim true :
          !obelisk_sim.managed_ref<i64, @Holder>, i64
      obelisk_sim.dynamic_override %field = %value owner %owner
          assign false claim false :
          !obelisk_sim.managed_ref<i64, @Holder>, i64
      obelisk_sim.release_override %field assign false :
          !obelisk_sim.managed_ref<i64, @Holder>
      obelisk_sim.release_override %field assign true :
          !obelisk_sim.managed_ref<i64, @Holder>
      obelisk_sim.return
    }
  }
}

// LLVM-LABEL: llvm.func @process(
// LLVM: llvm.call @obelisk_rt_v1_object_override
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_object_override
// LLVM-COUNT-2: llvm.call @obelisk_rt_v1_object_release_override
// LLVM-NOT: obelisk_sim.override
// LLVM-NOT: obelisk_sim.dynamic_override
// LLVM-NOT: obelisk_sim.release_override

// BYTECODE: intrinsic {{.*}}id=0x00010461 inputs=5 outputs=0 flags=0
// BYTECODE: intrinsic {{.*}}id=0x00010462 inputs=3 outputs=0 flags=0
// BYTECODE-COUNT-3: site {{.*}}id=0x00010461 inputs=
// BYTECODE-COUNT-2: site {{.*}}id=0x00010462 inputs=
