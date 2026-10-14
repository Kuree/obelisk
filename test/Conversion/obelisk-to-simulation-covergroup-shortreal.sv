// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=f32 replace-to=f64' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.sample operand=2 constant={0.0 : f64}' --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=SAMPLE

// Preserve shortreal types through capture, reference loads and sampling.
// IEEE 1800-2023 6.12, 19.3, 19.5 and 19.8.1 do not widen stored shortreal
// values.
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> f32
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<f32>
// CHECK-DAG: simulation.argument_ref.load {{.*}} -> f32
// CHECK-DAG: simulation.covergroup.sample {{.*}} f32
// CHECK-DAG: simulation.ref.alloc {{.*}} : f32 -> !simulation.ref<f32>
// CHECK-DAG: simulation.observer.bind @{{.*}}.$covergroup_event_sample.{{.*}}!simulation.ref<f32>, !simulation.argument_ref<f32>

// The schema records the shortreal formals and the sample expression as
// result_kind=18 with no integer width.
// SCHEMA-COUNT-5: functional_formal {{.*}} result_kind=18 {{.*}} width=0
// SCHEMA: functional_expression {{.*}} result_kind=18 width=0

// A shortreal formal read as f64, or a double passed as the sample value,
// fails the schema verifier shared by LLVM lowering and the bytecode encoder.
// TYPE: type-mismatched constructor FunctionalFormal ID
// SAMPLE: type-mismatched sample FunctionalExpression ID

module top;
  shortreal actual;
  bit clock;
  covergroup cg(input shortreal frozen = 0.125, ref shortreal live)
      with function sample(input shortreal current);
    cp: coverpoint current { bins low = {[0.0:0.5]}; }
    captured: coverpoint (frozen == 0.125 && live == current) { bins yes = {1}; }
  endgroup
  covergroup clocked(input shortreal frozen, ref shortreal live)
      @(posedge clock iff frozen == shortreal'(0.25));
    cp: coverpoint live { bins low = {[0.0:0.5]}; }
    captured: coverpoint (frozen == shortreal'(0.25)) { bins yes = {1}; }
  endgroup
  cg value;
  clocked observed;
  initial begin
    actual = 0.25;
    value = new(actual, actual);
    value.sample(actual);
    observed = new(actual, actual);
    actual = 0.5;
    #1; clock = 1;
    #1;
    $finish;
  end
endmodule
