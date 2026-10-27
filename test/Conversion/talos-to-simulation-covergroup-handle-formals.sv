// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.mlir --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.covergroup_handle<@__talos_covergroup_source_cg_4D2E260D7266D24C> replace-to=i64' '--test-talos-edit-attributes=op=simulation.covergroup.create match={argument_count = 2 : i32} operand=1 constant={0 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=NUMERIC
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.covergroup_handle<@__talos_covergroup_source_cg_4D2E260D7266D24C> replace-to=!simulation.covergroup_handle<@__talos_covergroup_cg_514F275A060A638E>' '--test-talos-edit-attributes=op=simulation.covergroup.create match={argument_count = 2 : i32} operand=1 poison=!simulation.covergroup_handle<@__talos_covergroup_cg_514F275A060A638E>' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOMINAL

// IEEE 1800-2023 19.3: typed covergroup-handle formals keep their nominal
// covergroup type across construction, ref loads and sample arguments.
// CHECK-DAG: simulation.covergroup.create
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.covergroup_handle<
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.covergroup_handle<
// CHECK-DAG: simulation.argument_ref.load {{.*}} -> !simulation.covergroup_handle<
// CHECK-DAG: simulation.covergroup.sample {{.*}} !simulation.covergroup_handle<

// The schema records the two constructor formals and the sample formal as
// handle-valued (result_kind=13, width 0), each naming the nominal covergroup
// type, plus the default expression of the first.
// SCHEMA-COUNT-3: functional_formal {{.*}} result_kind=13 {{.*}} width=0 {{.*}} value_type={{[1-9][0-9]*}}
// SCHEMA: functional_expression {{.*}} role=17 result_kind=13 width=0

// An integer is not a covergroup handle: reading either constructor formal as
// i64, or passing an i64 constructor payload, fails the schema verifier shared
// by the LLVM lowering and the bytecode encoder.
// NUMERIC-DAG: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
// NUMERIC-DAG: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID
// NUMERIC-DAG: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID

// Nominal identity matters too: an unrelated covergroup handle type (the
// enclosing covergroup's own type) is rejected for the same formal reads and
// for the first constructor payload.
// NOMINAL-DAG: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
// NOMINAL-DAG: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID
// NOMINAL-DAG: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID

module top;
  covergroup source_cg; cp: coverpoint 1; endgroup
  source_cg actual;
  covergroup cg(input source_cg frozen = null, ref source_cg live)
      with function sample(input source_cg current);
    cp: coverpoint (frozen == live && current == null) { bins nil = {1}; }
  endgroup
  cg value;
  initial begin
    actual = new;
    value = new(actual, actual);
    value.sample(null);
    $finish;
  end
endmodule
