// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.mailbox<i32,2539141311783288561> replace-to=i64' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.mailbox<i32,2539141311783288561> replace-to=!simulation.mailbox<!simulation.string,6610153785672721263>' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.create operand=1 constant={0 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=CREATE

// IEEE 1800-2023 19.3 and 15.4: mailbox handles are shared class-like
// handles, so covergroup formals keep the mailbox type, including its
// nominal specialization identity, across construction, ref loads and
// constructor defaults.
// CHECK-DAG: simulation.covergroup.create
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.mailbox{{<.*>}}
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.mailbox{{<.*>}}>
// CHECK-DAG: simulation.argument_ref.load {{.*}} -> !simulation.mailbox{{<.*>}}
// CHECK-DAG: simulation.covergroup.sample
// CHECK-DAG: simulation.mailbox.equal

// The schema records both constructor formals and the sample input as
// mailbox results (result_kind=17, width 0) over one container type
// descriptor of kind 4, plus the default expression.
// SCHEMA-COUNT-3: functional_formal {{.*}} result_kind=17 {{.*}} width=0
// SCHEMA: functional_container_type {{.*}} kind=4 {{.*}} bound=0
// SCHEMA: functional_expression {{.*}} role=17 result_kind=17 width=0

// Reading a formal as i64, or as a mailbox of another element type, fails
// the schema verifier shared by the LLVM lowering and the bytecode encoder;
// so does an i64 constructor payload.
// TYPE-COUNT-2: type-mismatched constructor FunctionalFormal ID
// CREATE: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0


module top;
  mailbox #(int) actual;
  covergroup cg(input mailbox #(int) frozen = null, ref mailbox #(int) live)
      with function sample(input mailbox #(int) current);
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
