// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s < %t/lowered.mlir
// RUN: %python %S/Inputs/dump-coverage-schema.py < %t/lowered.mlir \
// RUN:   | FileCheck %s --check-prefix=SCHEMA
// RUN: %python %S/Inputs/mutate-functional-batch.py formal-reorder \
// RUN:   < %t/lowered.mlir \
// RUN:   > %t/formal-reorder.mlir
// RUN: not obelisk-opt %t/formal-reorder.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=FORMAL-REORDER
// RUN: %python %S/Inputs/mutate-functional-batch.py constructor-expression-reorder \
// RUN:   < %t/lowered.mlir > %t/constructor-expression-reorder.mlir
// RUN: not obelisk-opt %t/constructor-expression-reorder.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CONSTRUCTOR-EXPRESSION-REORDER
// RUN: %python %S/Inputs/mutate-functional-batch.py constructor-expression-subset \
// RUN:   < %t/lowered.mlir > %t/constructor-expression-subset.mlir
// RUN: not obelisk-opt %t/constructor-expression-subset.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CONSTRUCTOR-EXPRESSION-SUBSET
// RUN: %python %S/Inputs/mutate-functional-batch.py sample-reorder \
// RUN:   < %t/lowered.mlir \
// RUN:   > %t/sample-reorder.mlir
// RUN: not obelisk-opt %t/sample-reorder.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SAMPLE-REORDER
// RUN: %python %S/Inputs/mutate-functional-batch.py sample-subset \
// RUN:   < %t/lowered.mlir \
// RUN:   > %t/sample-subset.mlir
// RUN: not obelisk-opt %t/sample-subset.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SAMPLE-SUBSET
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk -emit-obelisk %t/default-ref.sv -o %t/default-ref.mlir
// RUN: obelisk-opt %t/default-ref.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=DEFAULT-REF

// Constructor inputs are snapshotted while ref inputs retain a typed alias.
// Constructor-dependent range bounds are evaluated once and keyed by stable
// FunctionalExpression IDs rather than compiler-side bin ordinals. Omitted
// constructor and sample defaults reuse their already-evaluated actual value
// in the expression batch rather than evaluating the default a second time.
// CHECK: %[[SOURCE_REF:.*]] = obelisk_sim.argument_ref.from_ref
// CHECK: %[[SOURCE_VALUE:.*]] = obelisk_sim.ref.load
// CHECK: %[[HANDLE:.*]] = obelisk_sim.covergroup.create {{.*}} payloads[%[[SOURCE_REF]], %[[SOURCE_VALUE]], %[[SOURCE_VALUE]], {{.*}}, {{.*}}] argument_count 2 formal_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}] expression_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// CHECK: obelisk_sim.covergroup.formal_read {{.*}}[{{[1-9][0-9]*}}]
// CHECK: obelisk_sim.covergroup.sample {{.*}} values[{{.*}}, {{.*}}, {{.*}}] ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}, {{[1-9][0-9]*}}]
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=6 role=17 result_kind=2 width=32 signedness=2 owner_ordinal=0 owner_subordinal=0 phase=1 result_ordinal=0
// SCHEMA-DAG: functional_expression id={{[1-9][0-9]*}} owner={{[1-9][0-9]*}} owner_kind=6 role=17 result_kind=2 width=32 signedness=2 owner_ordinal=0 owner_subordinal=0 phase=2 result_ordinal=0
// FORMAL-REORDER: error: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
// CONSTRUCTOR-EXPRESSION-REORDER: error: does not contain the complete schema-ordered constructor FunctionalExpression batch
// CONSTRUCTOR-EXPRESSION-SUBSET: error: does not contain the complete schema-ordered constructor FunctionalExpression batch
// SAMPLE-REORDER: error: does not contain the complete schema-ordered sample FunctionalExpression batch
// SAMPLE-SUBSET: error: does not contain the complete schema-ordered sample FunctionalExpression batch
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_create
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_formal_read
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_sample

// A defaulted ref contributes its alias to the formal payload and the single
// value loaded through that alias to the FormalDefault expression batch.
// DEFAULT-REF: %[[REF:.*]] = obelisk_sim.argument_ref.from_ref
// DEFAULT-REF: %[[VALUE:.*]] = obelisk_sim.argument_ref.load %[[REF]]
// DEFAULT-REF: obelisk_sim.covergroup.create {{.*}} payloads[%[[REF]], %[[VALUE]], {{.*}}] argument_count 1 formal_ids [{{[1-9][0-9]*}}] expression_ids [{{[1-9][0-9]*}}, {{[1-9][0-9]*}}] : (!obelisk_sim.argument_ref<i32>, i32, i32)

//--- input.sv
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

//--- default-ref.sv
module default_ref;
  int source;
  covergroup cg(ref int live = source);
    cp: coverpoint live { bins zero = {0}; }
  endgroup
  cg cov;
  initial cov = new();
endmodule

//--- type-query.sv
module type_query_pending;
  bit sampled;
  real result;
  covergroup cg;
    cp: coverpoint sampled { bins one = {1}; }
  endgroup
  initial result = cg::get_coverage();
endmodule
