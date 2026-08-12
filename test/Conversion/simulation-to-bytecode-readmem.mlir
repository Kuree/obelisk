// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @readmem {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.read"
    obelisk_sim.func @read(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> (!obelisk_sim.logic<13>, i32, i64)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %fd = arith.constant 2147483648 : i32
      %data, %kind, %address =
          obelisk_sim.file.readmem_token %ctx, %fd {radix = 16 : i32} :
          (!obelisk_sim.context, i32) -> (!obelisk_sim.logic<13>, i32, i64)
      obelisk_sim.return %data, %kind, %address :
          !obelisk_sim.logic<13>, i32, i64
    }
  }
}

// CHECK: intrinsic 0: id=0x00010113 inputs=2 outputs=3 flags=0
