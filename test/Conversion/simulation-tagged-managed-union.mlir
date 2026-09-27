// RUN: obelisk-opt %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=INSTRUCTIONS
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=BYTECODE-SHELL
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -passes=verify -disable-output
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' -o %t.first
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' -o %t.second
// RUN: cmp %t.first %t.second

!tagged = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = true>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @tagged_managed_union {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"

    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Holder_value of @Holder at 0 : !tagged {
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
          !simulation.managed_ref<!tagged, @Holder>

      %object_arm = simulation.union.construct %node as 0 :
          (!simulation.class_handle<@Node>) -> !tagged
      // Keep a tagged aggregate containing a class handle live across a
      // collection point. The bytecode encoder must extract its conditional
      // managed root into a shadow before executing the safepoint.
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.managed.store %object_arm to %field :
          !tagged, !simulation.managed_ref<!tagged, @Holder>
      %loaded = simulation.managed.load %field :
          !simulation.managed_ref<!tagged, @Holder> -> !tagged
      %active = simulation.union.is_active %loaded[0] : !tagged
      %object = simulation.union.extract %loaded[0] :
          (!tagged) -> !simulation.class_handle<@Node>
      %loaded_bits = simulation.union.extract %loaded[1] :
          (!tagged) -> i32

      %bits = arith.constant 17 : i32
      %bits_arm = simulation.union.construct %bits as 1 : (i32) -> !tagged
      simulation.managed.store %bits_arm to %field :
          !tagged, !simulation.managed_ref<!tagged, @Holder>
      simulation.return
    }
  }
}

// SIM: simulation.class.field @Holder_value
// SIM: %[[OBJECT_ARM:.*]] = simulation.union.construct %{{.*}} as 0
// SIM: simulation.gc.safepoint
// SIM: simulation.managed.store %[[OBJECT_ARM]]
// SIM: simulation.union.extract %{{.*}}[0]
// SIM: %[[BITS_ARM:.*]] = simulation.union.construct %{{.*}} as 1
// SIM: simulation.managed.store %[[BITS_ARM]]

// The disjoint payload places the i32 arm at bit 64 and the two-bit tag at
// bit 128. The native lowering therefore shifts that arm while the managed
// class field transfers the 130-bit value's 17-byte store representation.
// NATIVE-LABEL: llvm.func @root(
// NATIVE: llvm.call @obelisk_rt_v1_object_allocate
// NATIVE: llvm.call @obelisk_rt_v1_object_allocate
// NATIVE: llvm.call @obelisk_rt_v1_gc_safepoint
// NATIVE: %[[STORE_SIZE:.*]] = llvm.mlir.constant(17 : i64)
// NATIVE-NEXT: %{{.*}} = llvm.call @obelisk_rt_v1_object_write({{.*}}, %[[STORE_SIZE]])
// NATIVE: %[[LOAD_SIZE:.*]] = llvm.mlir.constant(17 : i64)
// NATIVE-NEXT: %{{.*}} = llvm.call @obelisk_rt_v1_object_read({{.*}}, %[[LOAD_SIZE]])
// NATIVE: llvm.lshr
// NATIVE: %[[ARM_OFFSET:.*]] = llvm.mlir.constant(64 : i130)
// NATIVE-NEXT: %[[SHIFTED_BITS:.*]] = llvm.lshr %{{.*}}, %[[ARM_OFFSET]] : i130
// NATIVE-NEXT: %{{.*}} = llvm.trunc %[[SHIFTED_BITS]] : i130 to i32
// NATIVE: llvm.shl
// NATIVE: %[[SECOND_STORE_SIZE:.*]] = llvm.mlir.constant(17 : i64)
// NATIVE-NEXT: %{{.*}} = llvm.call @obelisk_rt_v1_object_write({{.*}}, %[[SECOND_STORE_SIZE]])
// NATIVE-NOT: simulation.union

// The bytecode encoder accepts the same managed class field and materializes
// aggregate root shadows around the safepoint. Exact instruction semantics are
// validated by the serialized-image verifier during this pass.
// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE: simulation.class.field @Holder_value
// BYTECODE: obelisk.bytecode.scratch_size = 312 : i64

// Managed insertion of arm 0 remains at offset zero. Arm 1 construction and
// extraction are ordinary numeric INSERT/EXTRACT records at bit offset 64.
// INSTRUCTIONS: opcode=22 flags=2 {{.*}} imm=0
// INSTRUCTIONS: opcode=21 flags=0 {{.*}} imm=64
// INSTRUCTIONS: opcode=22 flags=0 {{.*}} imm=64
// Tagged construction zeros inactive storage, so its live object slot is an
// exact managed root rather than a candidate word.
// INSTRUCTIONS: intrinsic {{.*}}id=0x00010411 inputs=2 outputs=1 flags=0

// A bytecode-only native shell derives the root and spawn symbols from the
// RootInitializer entry kind, so the noncanonical @root name remains valid.
// BYTECODE-SHELL: llvm.func @root.__obelisk_spawn
// BYTECODE-SHELL: llvm.call @root.__obelisk_spawn
// BYTECODE-SHELL-NOT: simulation.design
// BYTECODE-SHELL-NOT: llvm.func @root(
