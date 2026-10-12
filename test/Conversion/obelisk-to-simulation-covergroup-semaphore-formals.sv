// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.semaphore replace-to=i64' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.create operand=1 constant={0 : i64}' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=CREATE

// IEEE 1800-2023 19.3 and 15.3: semaphores are shared class handles, so
// covergroup formals keep the semaphore type across construction, ref loads
// and constructor defaults.
// CHECK-DAG: simulation.covergroup.create
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.semaphore
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.semaphore>
// CHECK-DAG: simulation.argument_ref.load {{.*}} -> !simulation.semaphore
// CHECK-DAG: simulation.covergroup.sample
// CHECK-DAG: simulation.semaphore.equal

// The schema records both constructor formals and the sample input as
// semaphore results (result_kind=16, width 0) plus the default expression.
// SCHEMA-COUNT-3: functional_formal {{.*}} result_kind=16 {{.*}} width=0
// SCHEMA: functional_expression {{.*}} role=17 result_kind=16 width=0

// Equal storage width does not make an i64 a semaphore: reading a formal as
// i64 or passing an i64 constructor payload fails the schema verifier shared
// by the LLVM lowering and the bytecode encoder.
// TYPE: type-mismatched constructor FunctionalFormal ID
// CREATE: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0

module top;
  semaphore actual;
  covergroup cg(input semaphore frozen = null, ref semaphore live)
      with function sample(input semaphore current);
    cp: coverpoint (frozen == live && current == null) { bins nil = {1}; }
  endgroup
  cg value;
  initial begin
    actual = new(1);
    value = new(actual, actual);
    value.sample(null);
    $finish;
  end
endmodule
