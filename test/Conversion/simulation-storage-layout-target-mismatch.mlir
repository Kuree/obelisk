// RUN: not obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines 2>&1 | FileCheck %s
// RUN: not obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' 2>&1 | FileCheck %s

// A serialized type from the i128:128 target cannot be reused on i128:64.
// Reject before either backend builds handles, frame offsets or bytecode.
!record = !simulation.unpacked_struct<[
  #simulation.field<name = "flag", type = i1, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "payload", type = i128, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "tail", type = i1, ordinal = 2, packedOffset = 0>
], #simulation.storage_layout<size = 48, alignment = 16, stride = 0,
                               offsets = [0, 16, 32]>>
module attributes {llvm.data_layout = "e-p:64:64-i64:64-i128:64"} {
  simulation.design @layout {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !record design
  }
}
// CHECK: aggregate storage layout disagrees with target LLVM data layout
