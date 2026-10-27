// RUN: talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s --check-prefix=LIVE
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.mlir --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read set={formal_id = 999 : i64}' --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-STRING-READ
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read set={formal_id = 999 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-STRING-READ

// IEEE 19.3 and 13.5.2: constructor refs stay read-only storage aliases while
// being passed to const-ref helpers. The later coverpoint reads through the
// same reference after the helper's other actual has changed the source.
// LIVE: simulation.spawn @[[PROCESS:unit_[0-9]+]]
// LIVE: simulation.func private @[[PROCESS]](
// LIVE: %[[LIVE:[0-9]+]] = simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<i32>
// LIVE-NOT: simulation.argument_ref.load %[[LIVE]]
// LIVE: simulation.call
// LIVE: %[[RESULT:[0-9]+]] = simulation.call {{.*}}%[[LIVE]]
// LIVE: %[[RESULT_VALUE:[0-9]+]] = simulation.logic.from_bits %[[RESULT]]
// LIVE: %[[CURRENT:[0-9]+]] = simulation.argument_ref.load %[[LIVE]]
// LIVE: %[[CURRENT_VALUE:[0-9]+]] = simulation.logic.from_bits %[[CURRENT]]
// LIVE: simulation.covergroup.sample {{.*}}values[%[[RESULT_VALUE]], %[[CURRENT_VALUE]]]
// NATIVE: llvm.call @talos_rt_v1_covergroup_formal_read
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> i192
// NATIVE: llvm.call @talos_rt_v1_argument_ref_load
// A read of a formal the schema does not declare is rejected by both backends.
// BAD-STRING-READ: error: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID

module top;
  int source = 1;
  function automatic int bump();
    source++;
    return 0;
  endfunction
  function automatic int read_current(const ref int target, input int ignored);
    return target;
  endfunction
  covergroup cg(ref int live);
    changed: coverpoint read_current(live, bump()) { bins two = {2}; }
    current: coverpoint live { bins two = {2}; }
  endgroup
  cg cov;
  initial begin
    cov = new(source);
    cov.sample();
  end
endmodule
