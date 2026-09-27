// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @strings {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.cast"
    simulation.func @cast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %text: !simulation.string {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %exact, %matched = simulation.string.to_packed_exact %text :
          (!simulation.string) -> (i24, i1)
      %ordinary = simulation.string.to_packed %text :
          (!simulation.string) -> i24
      simulation.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_string_to_packed
// NATIVE: llvm.call @obelisk_rt_v1_string_length
// NATIVE: llvm.icmp "eq"
// NATIVE: llvm.call @obelisk_rt_v1_string_to_packed
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010422 inputs=1 outputs=2 flags=1
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010422 inputs=1 outputs=1 flags=0
