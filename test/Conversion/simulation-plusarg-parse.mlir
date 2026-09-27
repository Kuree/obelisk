// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s --check-prefix=BYTECODE

// A 4096-bit destination remains one strict runtime parse in both execution
// tiers. Width changes alter only the result register / ABI byte count, never
// the generated operation count.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @plusarg_parse {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.parse"
    simulation.func private @parse(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (!simulation.logic<4096>, f64)
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %text = simulation.string.literal "1234"
      %logic = "simulation.plusarg.parse_logic"(%text) {radix = #simulation.radix<hex>} :
          (!simulation.string) -> !simulation.logic<4096>
      %real = "simulation.plusarg.parse_real"(%text) :
          (!simulation.string) -> f64
      simulation.return %logic, %real : !simulation.logic<4096>, f64
    }
  }
}

// NATIVE-LABEL: llvm.func @parse
// NATIVE-DAG: llvm.mlir.constant(4096 : i64)
// NATIVE-DAG: llvm.mlir.constant(512 : i64)
// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_plusarg_parse_logic
// NATIVE-COUNT-1: llvm.call @obelisk_rt_v1_plusarg_parse_real

// BYTECODE: intrinsic 1: id=0x00010116 inputs=2 outputs=1 flags=0
// BYTECODE: intrinsic 2: id=0x00010117 inputs=1 outputs=1 flags=0
