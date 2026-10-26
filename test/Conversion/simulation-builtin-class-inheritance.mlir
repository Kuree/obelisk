// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @builtin_inheritance {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "__obelisk_root"
    simulation.class.decl @S id 1 {
      builtin_base = @S_payload,
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @T id 2 extends @S {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @M id 3 {
      builtin_base = @M_payload,
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @S_payload of @S at 0 : !simulation.semaphore {
      is_static = false, is_weak = false
    }
    simulation.class.field @M_payload of @M at 0 : !simulation.mailbox<i32> {
      is_static = false, is_weak = false
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@T>
      %semaphore = simulation.class.builtin_upcast %object :
          !simulation.class_handle<@T> to !simulation.semaphore
      %restored = simulation.class.cast %semaphore :
          !simulation.semaphore to !simulation.class_handle<@T>
      %matches = simulation.class.is_instance %semaphore is @S :
          !simulation.semaphore
      %mailbox_object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@M>
      %mailbox = simulation.class.builtin_upcast %mailbox_object :
          !simulation.class_handle<@M> to !simulation.mailbox<i32>
      %mailbox_restored = simulation.class.cast %mailbox :
          !simulation.mailbox<i32> to !simulation.class_handle<@M>
      %mailbox_matches = simulation.class.is_instance %mailbox is @M :
          !simulation.mailbox<i32>
      simulation.return
    }
  }
}

// BYTECODE: runtime.bytecode_image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// BYTECODE: simulation.class.field @S_payload of @S at 0 offset 8 : !simulation.semaphore
// BYTECODE: simulation.class.field @M_payload of @M at 0 offset 8 : !simulation.mailbox<i32>
// BYTECODE: runtime.bytecode_function = 0 : i32

// Exact builtin trace kinds preserve the outer class reference through a
// semaphore/mailbox typed slot. Native descriptors also carry the payload
// location; descendants inherit it from their ordinary base descriptor.
// NATIVE-LABEL: llvm.mlir.global internal constant @M.__obelisk_class_descriptor
// NATIVE: llvm.insertvalue {{.*}}[15]
// NATIVE: %[[MKIND:.*]] = llvm.mlir.constant(1 : i32)
// NATIVE: llvm.insertvalue %[[MKIND]], {{.*}}[16]
// NATIVE: %[[MOFFSET:.*]] = llvm.mlir.constant(8 : i64)
// NATIVE: llvm.insertvalue %[[MOFFSET]], {{.*}}[18]
// NATIVE-LABEL: llvm.mlir.global internal constant @M.__obelisk_trace_entries
// NATIVE: %[[MSLOT:.*]] = llvm.mlir.constant(5 : i32)
// NATIVE: llvm.insertvalue %[[MSLOT]], {{.*}}[4]
// NATIVE-LABEL: llvm.mlir.global internal constant @T.__obelisk_class_descriptor
// NATIVE: llvm.mlir.addressof @S.__obelisk_class_descriptor
// NATIVE-LABEL: llvm.mlir.global internal constant @S.__obelisk_class_descriptor
// NATIVE: llvm.insertvalue {{.*}}[15]
// NATIVE: %[[SKIND:.*]] = llvm.mlir.constant(2 : i32)
// NATIVE: llvm.insertvalue %[[SKIND]], {{.*}}[16]
// NATIVE: %[[SOFFSET:.*]] = llvm.mlir.constant(8 : i64)
// NATIVE: llvm.insertvalue %[[SOFFSET]], {{.*}}[18]
// NATIVE-LABEL: llvm.mlir.global internal constant @S.__obelisk_trace_entries
// NATIVE: %[[SSLOT:.*]] = llvm.mlir.constant(6 : i32)
// NATIVE: llvm.insertvalue %[[SSLOT]], {{.*}}[4]
// NATIVE: llvm.call @obelisk_rt_v1_object_cast
// NATIVE: llvm.call @obelisk_rt_v1_object_is_instance
// NATIVE-NOT: simulation.class.builtin_upcast
