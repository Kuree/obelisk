// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.event replace-to=i64' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.create operand=1 constant={0 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=CREATE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.sample operand=2 constant={0 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=SAMPLE

// IEEE 1800-2023 19.3, 15.5 and 6.17: event covergroup formals keep their
// scheduler-event type across construction, ref loads and sample defaults.
// CHECK-DAG: simulation.covergroup.create
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.event
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.event>
// CHECK-DAG: simulation.argument_ref.load {{.*}} -> !simulation.event
// CHECK-DAG: simulation.covergroup.sample {{.*}} !simulation.event

// The schema records both constructor formals and the sample default as
// result_kind=12 with width 0, plus the default expression.
// SCHEMA-COUNT-3: functional_formal {{.*}} result_kind=12 {{.*}} width=0
// SCHEMA: functional_expression {{.*}} role=17 result_kind=12 width=0

// Equal storage width does not make an i64 an event: a mistyped formal read,
// constructor payload or sample value fails the schema verifier shared by the
// LLVM lowering and the bytecode encoder.
// TYPE-COUNT-2: type-mismatched constructor FunctionalFormal ID
// CREATE: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
// SAMPLE: type-mismatched sample FunctionalExpression ID

module top;
  event actual;
  covergroup cg(input event frozen, ref event live)
      with function sample(input event current = null);
    cp: coverpoint (frozen == live && current == null) { bins nil = {1}; }
  endgroup
  cg value;
  initial begin
    value = new(actual, actual);
    value.sample();
    $finish;
  end
endmodule
