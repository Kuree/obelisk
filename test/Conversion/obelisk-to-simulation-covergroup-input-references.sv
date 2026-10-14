// RUN: %obelisk --std=1800-2023 -emit-obelisk %s \
// RUN:   | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' \
// RUN:       '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' \
// RUN:       -o %t.lowered.mlir
// RUN: FileCheck %s --check-prefix=INPUT < %t.lowered.mlir
// RUN: obelisk-opt %t.lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=INPUT
// RUN: obelisk-opt %t.lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SHARED
// RUN: obelisk-opt %t.lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 19.3 and 19.8.1: an input is a variable holding a copied value. Allocate
// only when its address is used, at a point dominating conditional uses. Two
// distinct formals require independent copies even for the same SSA constant.
// Subsequent value reads load the copy after a writable helper call. Inputs
// used solely as values, including the constructor payload, stay in SSA.

// INPUT: %[[CTOR_COPY:[^ ]+]] = simulation.ref.alloc {{.*}} : i32 -> !simulation.ref<i32>
// INPUT: %[[CTOR_REF:[^ ]+]] = simulation.argument_ref.from_ref %[[CTOR_COPY]]
// INPUT: simulation.call @[[READ:unit_[0-9]+]](%{{.*}}, %[[CTOR_REF]])
// INPUT-NOT: simulation.ref.alloc
// INPUT: simulation.call @[[READ]](%{{.*}}, %[[CTOR_REF]])
// INPUT-NOT: simulation.ref.alloc
// INPUT: simulation.covergroup.create {{.*}} argument_count 2 {{.*}} : (i32, i32, i32, i32, i32)
// INPUT: %[[FROZEN:[^ ]+]] = simulation.covergroup.formal_read {{.*}} -> i32
// INPUT: %[[SCALAR:[^ ]+]] = simulation.covergroup.formal_read {{.*}} -> i32
// INPUT: %[[SAMPLE_COPY:[^ ]+]] = simulation.ref.alloc %[[FROZEN]]
// INPUT: %[[SAMPLE_REF:[^ ]+]] = simulation.argument_ref.from_ref %[[SAMPLE_COPY]]
// INPUT: simulation.call @[[READ]](%{{.*}}, %[[SAMPLE_REF]])
// INPUT-NOT: simulation.ref.alloc %[[SCALAR]]
// INPUT: simulation.covergroup.create {{.*}} argument_count 0
// INPUT: %[[LEFT:[^ ]+]] = simulation.ref.alloc {{.*}} : i32 -> !simulation.ref<i32>
// INPUT: %[[RIGHT:[^ ]+]] = simulation.ref.alloc {{.*}} : i32 -> !simulation.ref<i32>
// INPUT: cf.cond_br
// INPUT: %[[LEFT_REF:[^ ]+]] = simulation.argument_ref.from_ref %[[LEFT]]
// INPUT: simulation.call {{.*}}(%{{.*}}, %[[LEFT_REF]])
// INPUT-NOT: simulation.ref.alloc
// INPUT: simulation.ref.load %[[LEFT]] : !simulation.ref<i32> -> i32
// INPUT: %[[CURRENT:[^ ]+]] = simulation.ref.load %[[LEFT]] : !simulation.ref<i32> -> i32
// INPUT: %[[RIGHT_REF:[^ ]+]] = simulation.argument_ref.from_ref %[[RIGHT]]
// INPUT: %[[OTHER:[^ ]+]] = simulation.call @[[READ]](%{{.*}}, %[[RIGHT_REF]])
// INPUT: arith.addi %[[CURRENT]], %[[OTHER]]

// SHARED: simulation.covergroup.create {{.*}} argument_count 0
// SHARED: simulation.ref.alloc %[[SAME:[^ ]+]] : i32 -> !simulation.ref<i32>
// SHARED: simulation.ref.alloc %[[SAME]] : i32 -> !simulation.ref<i32>

module top;
  bit choose = 1;
  function automatic int read_int(const ref int target);
    return target;
  endfunction
  function automatic int bump(ref int target);
    target++;
    return target;
  endfunction
  covergroup captured(input int frozen, input int scalar);
    option.at_least = read_int(frozen);
    cp: coverpoint read_int(frozen) { bins one = {read_int(frozen)}; }
    plain: coverpoint scalar { bins seven = {7}; }
  endgroup
  covergroup copied with function sample(input int left, input int right,
                                         input bit branch);
    cp: coverpoint branch ? (bump(left) ? left + read_int(right) : -1) : left {
      bins both = {1, 3};
    }
  endgroup
  captured frozen;
  copied local_copy;
  initial begin
    frozen = new(1, 7);
    frozen.sample();
    local_copy = new;
    local_copy.sample(1, 1, choose);
  end
endmodule
