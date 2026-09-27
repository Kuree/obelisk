// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @event_create {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"

    simulation.class.decl @Holder id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.field @Holder_event of @Holder at 0 : !simulation.event {
      is_static = false, is_weak = false
    }

    simulation.func @process(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Holder>
      %event = simulation.event.create
      %field = simulation.class.field_ref %object[@Holder_event] :
          !simulation.class_handle<@Holder> ->
          !simulation.managed_ref<!simulation.event, @Holder>
      simulation.managed.store %event to %field :
          !simulation.event,
          !simulation.managed_ref<!simulation.event, @Holder>
      %loaded = simulation.managed.load %field :
          !simulation.managed_ref<!simulation.event, @Holder> ->
          !simulation.event
      %same = simulation.event.equal %event, %loaded
      simulation.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_scheduler_event_create
// NATIVE-LABEL: llvm.func @process
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_event_create
// NATIVE: llvm.call @obelisk_rt_v1_object_write
// NATIVE: llvm.call @obelisk_rt_v1_object_read

// BYTECODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// BYTECODE: simulation.class.field @Holder_event of @Holder at 0 offset 8 : !simulation.event
// BYTECODE: obelisk.bytecode.function = 0 : i32
