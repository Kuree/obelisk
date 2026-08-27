// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @strings {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.cast"
    obelisk_sim.func @cast(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %text: !obelisk_sim.string {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %exact, %matched = obelisk_sim.string.to_packed_exact %text :
          (!obelisk_sim.string) -> (i24, i1)
      %ordinary = obelisk_sim.string.to_packed %text :
          (!obelisk_sim.string) -> i24
      obelisk_sim.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_string_to_packed
// NATIVE: llvm.call @obelisk_rt_v1_string_length
// NATIVE: llvm.icmp "eq"
// NATIVE: llvm.call @obelisk_rt_v1_string_to_packed
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010422 inputs=1 outputs=2 flags=1
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010422 inputs=1 outputs=1 flags=0
