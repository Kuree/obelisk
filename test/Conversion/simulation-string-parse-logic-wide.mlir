// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

// Exact-width scan parsing shares the established arbitrary-width integral
// parser ABI in both execution tiers. Width 4096 changes only the output
// register and byte counts, never the number of runtime calls.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @string_parse_logic_wide {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.parse"
    obelisk_sim.func private @parse(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> !obelisk_sim.logic<4096>
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %text = obelisk_sim.string.literal "1234"
      %logic = obelisk_sim.string.parse_logic %text radix = 16 :
          !obelisk_sim.logic<4096>
      obelisk_sim.return %logic : !obelisk_sim.logic<4096>
    }
  }
}

// NATIVE-LABEL: llvm.func @parse
// NATIVE-DAG: llvm.mlir.constant(4096 : i64)
// NATIVE-DAG: llvm.mlir.constant(512 : i64)
// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_plusarg_parse_logic

// BYTECODE-COUNT-1: intrinsic 1: id=0x00010116 inputs=2 outputs=1 flags=0
