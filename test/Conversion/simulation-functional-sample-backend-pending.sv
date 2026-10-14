// RUN: obelisk -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' \
// RUN:   -o %t.lowered.mlir
// RUN: FileCheck %s --check-prefix=LOWERED < %t.lowered.mlir
// RUN: obelisk-opt %t.lowered.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %t.lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE
// RUN: not obelisk-opt %t.lowered.mlir \
// RUN:   '--test-obelisk-edit-attributes=op=simulation.covergroup.create operand=1 constant={0 : i32}' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-FORMAL
// RUN: not obelisk-opt %t.lowered.mlir \
// RUN:   '--test-obelisk-edit-attributes=op=simulation.covergroup.create operand=3 constant={0 : i32}' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-EXPRESSION
// RUN: not obelisk-opt %t.lowered.mlir \
// RUN:   '--test-obelisk-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.string replace-to=i32' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-READ
// RUN: not obelisk-opt %t.lowered.mlir \
// RUN:   '--test-obelisk-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from=!simulation.string replace-to=!simulation.argument_ref<!simulation.string>' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-STRING-READ

// The exact-v1 schema is embedded before either backend boundary. Both
// backends consume the schema-ordered FunctionalExpression batch; native
// lowering materializes typed descriptors for the runtime calls.
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

// A String constructor argument sampled by a coverpoint expression is read
// back as a String formal.
module string_sample;
  covergroup cg(input string configured_name);
    option.name = configured_name;
    cp: coverpoint configured_name.len() { bins one = {1}; }
  endgroup
  cg cov;
  initial begin
    cov = new("sampled string constructor");
    cov.sample();
  end
endmodule

// LOWERED-LABEL: simulation.covergroup.create
// LOWERED: simulation.covergroup.formal_read {{.*}} -> !simulation.string
// LOWERED: simulation.covergroup.sample
// LOWERED: simulation.covergroup.formal_read {{.*}} -> !simulation.string
// LOWERED: simulation.covergroup.sample
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_create
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_set_name
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_sample

// A String payload replaced by an i32 in the formal position, the expression
// position, or a formal read of the wrong type or reference form is rejected.
// BAD-STRING-FORMAL: error: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched
// BAD-STRING-EXPRESSION: error: has a type-mismatched constructor FunctionalExpression
// BAD-STRING-READ: error: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID
