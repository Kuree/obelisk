// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @procedural_path_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.code_unit.decl 16 in 0 function hierarchy "top.store"
    obelisk_sim.func @store(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %reference = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<2>>
      %value = obelisk_sim.logic.constant 1 : i2, 0 : i2 :
          !obelisk_sim.logic<2>
      %write = arith.constant 3 : i2
      %active = arith.constant 1 : i2
      %rise_mask = arith.constant 1 : i2
      %fall_mask = arith.constant 2 : i2
      %turnoff_mask = arith.constant 3 : i2
      %rise = obelisk_sim.time.constant 7
      %fall = obelisk_sim.time.constant 11
      %turnoff = obelisk_sim.time.constant 13
      obelisk_sim.ref.store_inertial_path %value to %reference write %write
          active %active masks [%rise_mask, %fall_mask, %turnoff_mask]
          after [%rise, %fall, %turnoff] site 55 : 0 group 0 of 1
          nonblocking = true : !obelisk_sim.ref<!obelisk_sim.logic<2>>,
          !obelisk_sim.logic<2>, i2
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @store
// CHECK: llvm.call @obelisk_rt_v1_scheduler_inertial_path_storage
// CHECK: llvm.call @obelisk_rt_v1_scheduler_fail
// CHECK-NOT: obelisk_sim.ref.store_inertial_path

// BYTECODE: intrinsic {{[0-9]+}}: id=0x00010243 inputs=15 outputs=0 flags=0
// BYTECODE: site {{[0-9]+}}: signature={{[0-9]+}} id=0x00010243 inputs={{\[[0-9, ]+\]}} outputs=[]
