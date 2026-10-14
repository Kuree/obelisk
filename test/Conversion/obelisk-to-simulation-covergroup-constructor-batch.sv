// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s --check-prefix=CHECK --check-prefix=DEFAULT-REF < %t.mlir
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.create match={argument_count = 2 : i32} set={formal_ids = array<i64: 1, 2>}' '--test-obelisk-edit-attributes=op=simulation.covergroup.create match={argument_count = 1 : i32} set={expression_ids = array<i64: 1, 2>}' '--test-obelisk-edit-attributes=op=simulation.covergroup.sample set={expression_ids = array<i64: 1, 2, 3>}' --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=REORDER
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.create match={argument_count = 2 : i32} operand=1 constant={0 : i32}' --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=REF-PAYLOAD

// Constructor inputs are snapshotted while ref inputs retain a typed alias.
// Constructor-dependent range bounds are evaluated once and keyed by stable
// FunctionalExpression IDs rather than compiler-side bin ordinals. Omitted
// constructor and sample defaults reuse their already-evaluated actual value
// in the expression batch rather than evaluating the default a second time.
// CHECK: %[[SOURCE_REF:.*]] = simulation.argument_ref.from_ref
// CHECK: %[[SOURCE_VALUE:.*]] = simulation.ref.load
// CHECK: %[[HANDLE:.*]] = simulation.covergroup.create {{.*}} payloads[%[[SOURCE_REF]], %[[SOURCE_VALUE]], %[[SOURCE_VALUE]], {{.*}}, {{.*}}] argument_count 2 formal_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}] expression_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// CHECK: simulation.covergroup.formal_read {{.*}}[{{[1-9][0-9]*}}]
// CHECK: simulation.covergroup.sample {{.*}} values[{{.*}}, {{.*}}, {{.*}}] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=6 role=17 result_kind=2 width=32 signedness=2 owner_ordinal=0 owner_subordinal=0 phase=1 result_ordinal=0
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=6 role=17 result_kind=2 width=32 signedness=2 owner_ordinal=0 owner_subordinal=0 phase=2 result_ordinal=0
// Bytecode encoding requires the complete schema-ordered batches. IDs that do
// not follow schema order are rejected for the formal, constructor-expression
// and sample batches (the IDs are replaced by ones outside the schema, as the
// real IDs depend on the source path).
// REORDER-DAG: error: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
// REORDER-DAG: error: does not contain the complete schema-ordered constructor FunctionalExpression batch
// REORDER-DAG: error: does not contain the complete schema-ordered sample FunctionalExpression batch
// A ref formal's payload must reference the formal's type: an integer in its
// place is rejected at schema ordinal 0 instead of reaching the backend.
// REF-PAYLOAD: error: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0

// The lowered batches are passed through native lowering unchanged.
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_create
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_formal_read
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_sample

// A defaulted ref contributes its alias to the formal payload and the single
// value loaded through that alias to the FormalDefault expression batch.
// DEFAULT-REF: %[[REF:.*]] = simulation.argument_ref.from_ref
// DEFAULT-REF: %[[VALUE:.*]] = simulation.argument_ref.load %[[REF]]
// DEFAULT-REF: simulation.covergroup.create {{.*}} payloads[%[[REF]], %[[VALUE]], {{.*}}] argument_count 1 formal_ids [{{[1-9][0-9]*}}] expression_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}] : (!simulation.argument_ref<i32>, i32, i32)

module constructor_batch;
  int source;
  covergroup cg(ref int live, input int snapshot = source)
      with function sample(input int enabled = 1);
    cp: coverpoint live iff (enabled) {
      bins configured = {[snapshot:live]};
    }
  endgroup
  cg cov;
  initial begin
    cov = new(source);
    cov.sample();
  end
endmodule

module default_ref;
  int source;
  covergroup default_cg(ref int live = source);
    cp: coverpoint live { bins zero = {0}; }
  endgroup
  default_cg cov;
  initial cov = new();
endmodule
