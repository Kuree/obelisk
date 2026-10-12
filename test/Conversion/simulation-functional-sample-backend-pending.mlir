// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   constructor-formal-string-type-mismatch < %t/lowered.mlir \
// RUN:   > %t/bad-string-formal.mlir
// RUN: not obelisk-opt %t/bad-string-formal.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-FORMAL
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   constructor-expression-string-type-mismatch < %t/lowered.mlir \
// RUN:   > %t/bad-string-expression.mlir
// RUN: not obelisk-opt %t/bad-string-expression.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-EXPRESSION
// RUN: %python %S/Inputs/mutate-functional-batch.py insert-string-formal-read \
// RUN:   < %t/lowered.mlir > %t/string-read.mlir
// RUN: obelisk-opt %t/string-read.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk-opt %t/string-read.mlir --convert-obelisk-sim-processes-to-llvm-coroutines > /dev/null
// RUN: %python %S/Inputs/mutate-functional-batch.py insert-string-formal-read-wrong-type \
// RUN:   < %t/lowered.mlir > %t/bad-string-read.mlir
// RUN: not obelisk-opt %t/bad-string-read.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-READ
// RUN: obelisk -emit-obelisk %t/string-sample.sv -o %t/string-sample.mlir
// RUN: obelisk-opt %t/string-sample.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=STRING-SAMPLE

// The exact-v1 schema is embedded before either backend boundary. Both
// backends consume the schema-ordered FunctionalExpression batch; native
// lowering materializes typed descriptors for the runtime calls.
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_create
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_set_name
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_sample
// BAD-STRING-FORMAL: error: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched
// BAD-STRING-EXPRESSION: error: has a type-mismatched constructor FunctionalExpression
// BAD-STRING-READ: error: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID
// STRING-SAMPLE: simulation.covergroup.formal_read {{.*}} -> !simulation.string

//--- input.sv
module functional_sample_backend;
  bit [7:0] value;
  covergroup cg(input string configured_name);
    option.name = configured_name;
    cp: coverpoint value { bins one = {1}; }
  endgroup
  cg cov;
  initial begin
    cov = new("managed-initial-name");
    cov.set_inst_name("managed-renamed-name");
    cov.sample();
  end
endmodule

//--- string-sample.sv
module string_sample;
  covergroup cg(input string configured_name);
    cp: coverpoint configured_name.len();
  endgroup
  cg cov;
  initial begin
    cov = new("sampled string constructor");
    cov.sample();
  end
endmodule
