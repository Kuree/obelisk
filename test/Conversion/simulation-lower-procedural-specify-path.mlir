// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @procedural_path_lowering {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<2> design
    simulation.code_unit.decl 16 in 0 function hierarchy "top.store"
    simulation.func @store(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %reference = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<2>>
      %value = simulation.logic.constant 1 : i2, 0 : i2 :
          !simulation.logic<2>
      %write = arith.constant 3 : i2
      %active = arith.constant 1 : i2
      %rise_mask = arith.constant 1 : i2
      %fall_mask = arith.constant 2 : i2
      %turnoff_mask = arith.constant 3 : i2
      %rise = simulation.time.constant 7
      %fall = simulation.time.constant 11
      %turnoff = simulation.time.constant 13
      simulation.ref.store_inertial_path %value to %reference write %write
          active %active masks [%rise_mask, %fall_mask, %turnoff_mask]
          after [%rise, %fall, %turnoff] site 55 : 0 group 0 of 1
          nonblocking = true : !simulation.ref<!simulation.logic<2>>,
          !simulation.logic<2>, i2
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @store
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_path_storage
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: simulation.ref.store_inertial_path

// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010245 inputs=15 outputs=0 flags=0
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010245 inputs={{\[[0-9, ]+\]}} outputs=[]
