// RUN: obelisk-opt %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=INSTRUCTIONS

!tagged = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = true>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @tagged_managed_union {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"

    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Holder_value of @Holder at 0 : !tagged {
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
          !obelisk_sim.managed_ref<!tagged, @Holder>

      %object_arm = obelisk_sim.union.construct %node as 0 :
          (!obelisk_sim.class_handle<@Node>) -> !tagged
      obelisk_sim.managed.store %object_arm to %field :
          !tagged, !obelisk_sim.managed_ref<!tagged, @Holder>
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %loaded = obelisk_sim.managed.load %field :
          !obelisk_sim.managed_ref<!tagged, @Holder> -> !tagged
      %active = obelisk_sim.union.is_active %loaded[0] : !tagged
      %object = obelisk_sim.union.extract %loaded[0] :
          (!tagged) -> !obelisk_sim.class_handle<@Node>
      %loaded_bits = obelisk_sim.union.extract %loaded[1] :
          (!tagged) -> i32

      %bits = arith.constant 17 : i32
      %bits_arm = obelisk_sim.union.construct %bits as 1 : (i32) -> !tagged
      obelisk_sim.managed.store %bits_arm to %field :
          !tagged, !obelisk_sim.managed_ref<!tagged, @Holder>
      obelisk_sim.return
    }
  }
}

// SIM: obelisk_sim.class.field @Holder_value
// SIM: %[[OBJECT_ARM:.*]] = obelisk_sim.union.construct %{{.*}} as 0
// SIM: obelisk_sim.managed.store %[[OBJECT_ARM]]
// SIM: obelisk_sim.gc.safepoint
// SIM: obelisk_sim.union.extract %{{.*}}[0]
// SIM: %[[BITS_ARM:.*]] = obelisk_sim.union.construct %{{.*}} as 1
// SIM: obelisk_sim.managed.store %[[BITS_ARM]]

// The disjoint payload places the i32 arm at bit 64 and the two-bit tag at
// bit 128. The native lowering therefore shifts that arm while the managed
// class field transfers the 130-bit value's 17-byte store representation.
// NATIVE-LABEL: llvm.func @root(
// NATIVE: llvm.call @obelisk_rt_v1_object_allocate
// NATIVE: llvm.call @obelisk_rt_v1_object_allocate
// NATIVE: %[[STORE_SIZE:.*]] = llvm.mlir.constant(17 : i64)
// NATIVE-NEXT: %{{.*}} = llvm.call @obelisk_rt_v1_object_write({{.*}}, %[[STORE_SIZE]])
// NATIVE: llvm.call @obelisk_rt_v1_gc_safepoint
// NATIVE: %[[LOAD_SIZE:.*]] = llvm.mlir.constant(17 : i64)
// NATIVE-NEXT: %{{.*}} = llvm.call @obelisk_rt_v1_object_read({{.*}}, %[[LOAD_SIZE]])
// NATIVE: llvm.lshr
// NATIVE: %[[ARM_OFFSET:.*]] = llvm.mlir.constant(64 : i130)
// NATIVE-NEXT: %[[SHIFTED_BITS:.*]] = llvm.lshr %{{.*}}, %[[ARM_OFFSET]] : i130
// NATIVE-NEXT: %{{.*}} = llvm.trunc %[[SHIFTED_BITS]] : i130 to i32
// NATIVE: llvm.shl
// NATIVE: %[[SECOND_STORE_SIZE:.*]] = llvm.mlir.constant(17 : i64)
// NATIVE-NEXT: %{{.*}} = llvm.call @obelisk_rt_v1_object_write({{.*}}, %[[SECOND_STORE_SIZE]])
// NATIVE-NOT: obelisk_sim.union

// The bytecode encoder accepts the same managed class field and materializes
// aggregate root shadows around the safepoint. Exact instruction semantics are
// validated by the serialized-image verifier during this pass.
// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE: obelisk_sim.class.field @Holder_value
// BYTECODE: obelisk.bytecode.scratch_size = 304 : i64

// Managed insertion of arm 0 remains at offset zero. Arm 1 construction and
// extraction are ordinary numeric INSERT/EXTRACT records at bit offset 64.
// INSTRUCTIONS: opcode=22 flags=2 {{.*}} imm=0
// INSTRUCTIONS: opcode=21 flags=0 {{.*}} imm=64
// INSTRUCTIONS: opcode=22 flags=0 {{.*}} imm=64
