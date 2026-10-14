// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--test-obelisk-edit-attributes=replace-from="\"s4\"" replace-to="\"group\""' \
// RUN:   '--test-obelisk-edit-attributes=replace-from="\"s48\"" replace-to="\"group\""' -o %t.scoped.mlir
// RUN: obelisk-opt %t.scoped.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.lowered.mlir
// RUN: FileCheck %s < %t.lowered.mlir
// RUN: obelisk-opt %t.scoped.mlir --mlir-disable-threading '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.lowered.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 19.4: distinct classes may embed different covergroups with the same
// name. MLIR leaf symbols are scoped; complete type paths identify the correct
// constructor argument list and sample body. The edits turn the frontend's
// globally numbered leaf names into equal symbols in two separate symbol
// tables, so only the complete path distinguishes Alpha's group from Beta's.

// CHECK: simulation.covergroup.create {{.*}} from @[[ALPHA:__obelisk_covergroup_group_[A-Z0-9]+]] {{.*}} argument_count 1 {{.*}} : (i32, i32)
// CHECK: simulation.covergroup.create {{.*}} from @[[BETA:__obelisk_covergroup_group_[A-Z0-9]+]] {{.*}} argument_count 2 {{.*}} : (i32, i32, i32)
// CHECK: simulation.covergroup.formal_read {{.*}} !simulation.covergroup_handle<@[[ALPHA]]> -> i32
// CHECK: simulation.covergroup.sample {{.*}} !simulation.covergroup_handle<@[[ALPHA]]>
// CHECK: simulation.covergroup.formal_read {{.*}} !simulation.covergroup_handle<@[[BETA]]> -> i32
// CHECK: simulation.covergroup.formal_read {{.*}} !simulation.covergroup_handle<@[[BETA]]> -> i32
// CHECK: simulation.covergroup.sample {{.*}} !simulation.covergroup_handle<@[[BETA]]>

class Alpha;
  covergroup cg(input int seed);
    cp: coverpoint seed { bins zero = {0}; }
  endgroup
  function new;
    cg = new(0);
  endfunction
endclass
class Beta;
  covergroup cg(input int seed, input int other);
    cp: coverpoint seed + other { bins one = {1}; }
  endgroup
  function new;
    cg = new(0, 1);
  endfunction
endclass
module top;
  Alpha a;
  Beta b;
  initial begin
    a = new;
    b = new;
    a.cg.sample();
    b.cg.sample();
    $finish;
  end
endmodule

