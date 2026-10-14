// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--test-obelisk-edit-attributes=replace-from="\"s4.cg\"" replace-to="\"group\""' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="\"s33.cg\"" replace-to="\"group\""' -o %t.scoped.mlir
// RUN: obelisk-opt %t.scoped.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.lowered.mlir
// RUN: FileCheck %s < %t.lowered.mlir
// RUN: obelisk-opt %t.scoped.mlir --mlir-disable-threading '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 19.4: package covergroups with equal leaf names are scoped by their
// package; the edits make the two leaf symbols equal. Static package queries
// have no typed receiver and resolve their complete source symbol paths
// outside the outlined function's symbol table. A user function named sample
// is identified by its prepared callee even when its first actual is a
// covergroup handle (IEEE 13.5).

// CHECK: simulation.covergroup.decl @[[FIRST:__obelisk_covergroup_group_[A-Z0-9]+]]
// CHECK: simulation.covergroup.decl @[[SECOND:__obelisk_covergroup_group_[A-Z0-9]+]]
// CHECK: simulation.func private @[[HELPER:unit_[0-9]+]]({{.*}}simulation.hierarchical_name = "top.sample"
// CHECK: simulation.covergroup.create {{.*}} from @[[FIRST]] {{.*}} argument_count 1
// CHECK: simulation.covergroup.create {{.*}} from @[[SECOND]] {{.*}} argument_count 2
// CHECK: simulation.call @[[HELPER]]
// CHECK: simulation.covergroup.type_query {{.*}} from @[[FIRST]] item 0
// CHECK: simulation.covergroup.type_query {{.*}} from @[[SECOND]] item 0
// CHECK: simulation.covergroup.type_query {{.*}} from @[[FIRST]] item {{[1-9][0-9]*}}
// CHECK: simulation.covergroup.type_query {{.*}} from @[[SECOND]] item {{[1-9][0-9]*}}

package First;
  covergroup cg(input int value);
    cp: coverpoint value { bins zero = {0}; }
  endgroup
endpackage
package Second;
  covergroup cg(input int value, input int other);
    cp: coverpoint value + other { bins one = {1}; }
  endgroup
endpackage
module top;
  First::cg a;
  Second::cg b;
  int calls;
  function automatic void sample(input First::cg group, input int value);
    calls += value;
    group.sample();
  endfunction
  initial begin
    a = new(0);
    b = new(0, 1);
    sample(a, 1);
    b.sample();
    $display("type=%0.1f,%0.1f point=%0.1f,%0.1f",
             First::cg::get_coverage(), Second::cg::get_coverage(),
             First::cg::cp::get_coverage(), Second::cg::cp::get_coverage());
    $finish;
  end
endmodule

