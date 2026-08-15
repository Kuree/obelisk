// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @virtual_interface_scope {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.bus" interface "@bus"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.scope"
    obelisk_sim.func @scope(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %handle = obelisk_sim.virtual_interface.bind 1
          : !obelisk_sim.virtual_interface<"@bus", "">
      %scope = obelisk_sim.virtual_interface.scope %handle
          : !obelisk_sim.virtual_interface<"@bus", "">
      %descriptor = arith.constant 0 : i32
      obelisk_sim.display %ctx to %descriptor(%scope) newline = false radix = 10
          flags = [0] : i64
      %format = obelisk_sim.bytes.constant "%p"
      %formatted = obelisk_sim.string.output_format %ctx(%format, %handle)
          radix = 10 flags = [32, 256] : !obelisk_sim.bytes,
          !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.display %ctx to %descriptor(%handle) newline = false
          radix = 10 flags = [256] :
          !obelisk_sim.virtual_interface<"@bus", "">
      obelisk_sim.return
    }
  }
}

// NATIVE-LABEL: llvm.func @scope
// NATIVE: llvm.mlir.constant(1 : i64)
// NATIVE: %[[VIF_KIND:.*]] = llvm.mlir.constant(8 : i32) : i32
// NATIVE: llvm.insertvalue %[[VIF_KIND]], %{{.*}}[0]
// NATIVE: llvm.call @obelisk_rt_v1_string_output_format
// NATIVE: llvm.mlir.constant(8 : i32) : i32
// NATIVE: llvm.call @obelisk_rt_v1_display
// NATIVE-NOT: obelisk_sim.virtual_interface

// The bind is a constant and scope projection is a representation-preserving
// move, so both operations remain explicit in bytecode instruction selection.
// BYTECODE: opcode=1
// BYTECODE: opcode=2
