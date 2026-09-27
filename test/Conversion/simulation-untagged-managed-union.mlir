// RUN: obelisk-opt %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=INSTRUCTIONS

!choice = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "bits", type = !simulation.logic<64>, ordinal = 2, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @untagged_managed_union {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.storage.decl 0 in 0 : !choice design hierarchy "top.shared"

    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Holder id 2 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Holder_value of @Holder at 0 : !choice {
      is_static = false, is_weak = false
    }

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %holder = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Holder>
      %node = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Node>
      %value = simulation.union.construct %node as 0 :
          (!simulation.class_handle<@Node>) -> !choice

      %shared = simulation.context.storage %ctx[0] :
          !simulation.ref<!choice>
      simulation.ref.store %value to %shared :
          !choice, !simulation.ref<!choice>
      %field = simulation.class.field_ref %holder[@Holder_value] :
          !simulation.class_handle<@Holder> ->
          !simulation.managed_ref<!choice, @Holder>
      simulation.managed.store %value to %field :
          !choice, !simulation.managed_ref<!choice, @Holder>

      %local = simulation.ref.alloc %value :
          !choice -> !simulation.ref<!choice>
      simulation.gc.safepoint %ctx : !simulation.context
      %live_raw = simulation.union.extract %value[0] :
          (!choice) -> !simulation.class_handle<@Node>
      %live_checked = simulation.class.cast %live_raw :
          !simulation.class_handle<@Node> to
          !simulation.class_handle<@Node>
      %loaded = simulation.ref.load %local :
          !simulation.ref<!choice> -> !choice
      %raw = simulation.union.extract %loaded[0] :
          (!choice) -> !simulation.class_handle<@Node>
      %checked = simulation.class.cast %raw :
          !simulation.class_handle<@Node> to
          !simulation.class_handle<@Node>
      %is_node = simulation.class.is_instance %checked is @Node :
          !simulation.class_handle<@Node>

      // Untagged member assignment is a preserving read-modify-write. It must
      // not clear the bytes outside the selected arm as union.construct does.
      // A four-state member shares its value plane with the candidate handle
      // word. Its unknown plane never participates in candidate validation.
      %bits = simulation.logic.constant 17 : i64, -1 : i64 :
          !simulation.logic<64>
      %updated = simulation.aggregate.insert %bits into %loaded[2] :
          (!choice, !simulation.logic<64>) -> !choice
      simulation.ref.store %updated to %local :
          !choice, !simulation.ref<!choice>
      simulation.return
    }
  }
}

// SIM: simulation.storage.decl 0
// SIM: simulation.class.field @Holder_value
// SIM: simulation.class.cast
// SIM: %[[UPDATED:.*]] = simulation.aggregate.insert %{{.*}} into %{{.*}}[2]
// SIM: simulation.ref.store %[[UPDATED]]

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
// BYTECODE: simulation.class.field @Holder_value

// Candidate SSA rooting uses MANAGED_CANDIDATE_ROOT. Automatic state receives
// offset/mask/flag triples through STATE_ALLOC_TYPED (0x00010230).
// INSTRUCTIONS: id=0x00010230 inputs=4 outputs=1 flags=0
// INSTRUCTIONS: id=0x00010414 inputs=3 outputs=1 flags=0
