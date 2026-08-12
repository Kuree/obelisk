// RUN: obelisk-opt %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=INSTRUCTIONS

!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i64, ordinal = 2, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @untagged_managed_union {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    obelisk_sim.storage.decl 0 in 0 : !choice design hierarchy "top.shared"

    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Holder_value of @Holder at 0 : !choice {
      is_static = false, is_weak = false
    }

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %holder = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Holder>
      %node = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Node>
      %value = obelisk_sim.union.construct %node as 0 :
          (!obelisk_sim.class_handle<@Node>) -> !choice

      %shared = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!choice>
      obelisk_sim.ref.store %value to %shared :
          !choice, !obelisk_sim.ref<!choice>
      %field = obelisk_sim.class.field_ref %holder[@Holder_value] :
          !obelisk_sim.class_handle<@Holder> ->
          !obelisk_sim.managed_ref<!choice, @Holder>
      obelisk_sim.managed.store %value to %field :
          !choice, !obelisk_sim.managed_ref<!choice, @Holder>

      %local = obelisk_sim.ref.alloc %value :
          !choice -> !obelisk_sim.ref<!choice>
      obelisk_sim.gc.safepoint %ctx : !obelisk_sim.context
      %live_raw = obelisk_sim.union.extract %value[0] :
          (!choice) -> !obelisk_sim.class_handle<@Node>
      %live_checked = obelisk_sim.class.cast %live_raw :
          !obelisk_sim.class_handle<@Node> to
          !obelisk_sim.class_handle<@Node>
      %loaded = obelisk_sim.ref.load %local :
          !obelisk_sim.ref<!choice> -> !choice
      %raw = obelisk_sim.union.extract %loaded[0] :
          (!choice) -> !obelisk_sim.class_handle<@Node>
      %checked = obelisk_sim.class.cast %raw :
          !obelisk_sim.class_handle<@Node> to
          !obelisk_sim.class_handle<@Node>
      %is_node = obelisk_sim.class.is_instance %checked is @Node :
          !obelisk_sim.class_handle<@Node>

      // Untagged member assignment is a preserving read-modify-write. It must
      // not clear the bytes outside the selected arm as union.construct does.
      %bits = arith.constant 17 : i64
      %updated = obelisk_sim.aggregate.insert %bits into %loaded[2] :
          (!choice, i64) -> !choice
      obelisk_sim.ref.store %updated to %local :
          !choice, !obelisk_sim.ref<!choice>
      obelisk_sim.return
    }
  }
}

// SIM: obelisk_sim.storage.decl 0
// SIM: obelisk_sim.class.field @Holder_value
// SIM: obelisk_sim.class.cast
// SIM: %[[UPDATED:.*]] = obelisk_sim.aggregate.insert %{{.*}} into %{{.*}}[2]
// SIM: obelisk_sim.ref.store %[[UPDATED]]

// The same conditional slot is used by the class descriptor, static design
// state, SSA shadow root, and automatic state allocation.
// NATIVE-LABEL: llvm.func @__obelisk_root(
// NATIVE: llvm.call @obelisk_rt_v1_native_state_alloc_with_typed_roots
// NATIVE: llvm.call @obelisk_rt_v1_gc_candidate_root
// NATIVE: llvm.call @obelisk_rt_v1_gc_safepoint
// NATIVE: llvm.call @obelisk_rt_v1_object_cast
// NATIVE-LABEL: llvm.func @main(
// NATIVE: llvm.call @obelisk_rt_v1_gc_candidate_static_root_register

// The encoded image retains the class field and the candidate static root.
// Automatic state uses the append-only typed allocation intrinsic.
// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE: obelisk_sim.class.field @Holder_value

// Candidate SSA rooting uses MANAGED_CANDIDATE_ROOT. Automatic state receives
// offset/mask/flag triples through STATE_ALLOC_TYPED (0x00010230).
// INSTRUCTIONS: id=0x00010230 inputs=4 outputs=1 flags=0
// INSTRUCTIONS: id=0x00010414 inputs=3 outputs=1 flags=0
