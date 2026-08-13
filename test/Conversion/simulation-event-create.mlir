// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @event_create {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"

    obelisk_sim.class.decl @Holder id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.field @Holder_event of @Holder at 0 : !obelisk_sim.event {
      is_static = false, is_weak = false
    }

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Holder>
      %event = obelisk_sim.event.create
      %field = obelisk_sim.class.field_ref %object[@Holder_event] :
          !obelisk_sim.class_handle<@Holder> ->
          !obelisk_sim.managed_ref<!obelisk_sim.event, @Holder>
      obelisk_sim.managed.store %event to %field :
          !obelisk_sim.event,
          !obelisk_sim.managed_ref<!obelisk_sim.event, @Holder>
      %loaded = obelisk_sim.managed.load %field :
          !obelisk_sim.managed_ref<!obelisk_sim.event, @Holder> ->
          !obelisk_sim.event
      %same = obelisk_sim.event.equal %event, %loaded
      obelisk_sim.return
    }
  }
}

// NATIVE-DAG: llvm.func @obelisk_rt_v1_scheduler_event_create
// NATIVE-LABEL: llvm.func @process
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_event_create
// NATIVE: llvm.call @obelisk_rt_v1_object_write
// NATIVE: llvm.call @obelisk_rt_v1_object_read

// BYTECODE: obelisk.bytecode.image = array<i8: 79, 66, 66, 67, 68, 83, 49, 0
// BYTECODE: obelisk_sim.class.field @Holder_event of @Holder at 0 offset 8 : !obelisk_sim.event
// BYTECODE: obelisk.bytecode.function = 0 : i32
