// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' \
// RUN:   | %python %S/Inputs/dump-bytecode-instructions.py \
// RUN:   | FileCheck %s

// IEEE 1800-2017 20.15.1 `$random` keeps its Annex N state independent of the
// process-local RNG. Ensure bytecode retains that dedicated intrinsic.
// CHECK: intrinsic 0: id=0x00010467 inputs=0 outputs=1 flags=0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @legacy_random {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "legacy_random.draw"

    simulation.func private @draw(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32}) -> i32
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %result = simulation.random.legacy %ctx
          : (!simulation.context) -> i32
      simulation.return %result : i32
    }
  }
}
