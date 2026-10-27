// RUN: talos-opt %s --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=STATE --implicit-check-not=__talos_state_unknown
// RUN: talos-opt %s --encode-talos-sim-to-bytecode='vpi=off require-bytecode=true' --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=BYTECODE --implicit-check-not=__talos_state_unknown
// RUN: talos-opt %S/simulation-eval-clock-cohort.mlir '--test-talos-set-module-attributes=attrs="{native.two_state}"' | talos-opt --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-suspension),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' -o %t.cohort.mlir
// RUN: FileCheck %s --check-prefix=NBA --implicit-check-not=__talos_state_unknown --implicit-check-not=__talos_eval_nba_unknown_ --implicit-check-not='native.nba_field' < %t.cohort.mlir
// RUN: talos-translate --mlir-to-llvmir %t.cohort.mlir | opt -passes=verify -disable-output

// Integer storage plus its private unaligned-access guard occupies one plane.
// The execution flag tells both runtime tiers that a null unknown pointer is
// an implicit zero plane, rather than missing backing storage.
// STATE: runtime.execution_flags = #runtime.execution_flags<two_state>
// STATE: llvm.mlir.global internal @__talos_state_value()
// STATE: llvm.mlir.zero : !llvm.array<131080 x i8>
// BYTECODE: runtime.execution_flags = #runtime.execution_flags<has_bytecode|require_bytecode|two_state>
// BYTECODE: llvm.mlir.global internal @__talos_state_value()
// BYTECODE: llvm.mlir.zero : !llvm.array<131080 x i8>

// Accumulators retain value, write mask, validity, region and transient-change
// fields, with the 32-byte unknown field physically removed (136 -> 104).
// NBA: llvm.mlir.global internal @__talos_aot_nba_accumulator_{{[0-9]+}}()
// NBA: llvm.mlir.zero : !llvm.array<104 x i8>
// NBA-NOT: !llvm.array<136 x i8>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.two_state
} {
  simulation.design @value_only {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1048576 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "value_only.root"
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}
