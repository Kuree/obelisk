// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @coverage_control {
    simulation.scope.decl 0 hierarchy "$root" coverage_id 42
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %control = arith.constant 3 : i32
      %metric = arith.constant 23 : i32
      %scope = arith.constant 11 : i32
      %definition = simulation.string.literal "DUT"
      %database = simulation.string.literal "run.obcov"
      %a = simulation.coverage.control_definition %ctx control %control metric %metric scope %scope definition %definition : !simulation.context
      %b = simulation.coverage.control_instance %ctx control %control metric %metric scope %scope instance 42 : !simulation.context
      %c = simulation.coverage.query_definition %ctx metric %metric scope %scope definition %definition maximum true : !simulation.context
      %d = simulation.coverage.query_instance %ctx metric %metric scope %scope instance 42 maximum false : !simulation.context
      %e = simulation.coverage.save %ctx metric %metric name %database : !simulation.context
      %f = simulation.coverage.merge %ctx metric %metric name %database : !simulation.context
      simulation.return
    }
  }
}

// NATIVE: llvm.call @obelisk_rt_v1_coverage_control_definition
// NATIVE: llvm.call @obelisk_rt_v1_coverage_control_instance
// NATIVE: llvm.call @obelisk_rt_v1_coverage_query_definition
// NATIVE: llvm.call @obelisk_rt_v1_coverage_query_instance
// NATIVE: llvm.call @obelisk_rt_v1_coverage_database_save
// NATIVE: llvm.call @obelisk_rt_v1_coverage_database_merge
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001046c inputs=4 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001046d inputs=4 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001046e inputs=4 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001046f inputs=4 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010470 inputs=2 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010471 inputs=2 outputs=1 flags=0
