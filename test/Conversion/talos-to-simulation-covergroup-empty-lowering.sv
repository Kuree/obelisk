// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--lower-talos-to-sim=opt-level=0' -o %t.mlir
// RUN: talos-opt %t.mlir --canonicalize | FileCheck %s --check-prefix=IR
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.mlir --encode-talos-sim-to-bytecode='vpi=off' --test-talos-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 19.11 permits an empty group. Verify its compiler boundary using the
// embedded schema, empty constructor batches, and all three query results.
// IR: simulation.covergroup.create {{.*}} payloads[] argument_count 0 formal_ids [] expression_ids []
// IR: simulation.covergroup.instance_query
// IR: simulation.covergroup.type_query
// SCHEMA: functional_type id={{[1-9][0-9]*}}
// SCHEMA-NOT: functional_item
// SCHEMA-NOT: functional_bin

// NATIVE-LABEL: llvm.func @unit_{{[0-9]+}}({{.*}}native.entry_kind = 1 : i32
// NATIVE: %[[NULL:.*]] = llvm.mlir.zero : !llvm.ptr
// NATIVE: %[[TYPE:.*]] = llvm.mlir.constant({{[1-9][0-9]*}} : i64)
// NATIVE: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64)
// NATIVE: %[[ZERO2:.*]] = llvm.mlir.constant(0 : i64)
// NATIVE: llvm.call @talos_rt_v1_covergroup_create({{.*}}, %[[TYPE]], %[[NULL]], %[[ZERO]], %[[NULL]], %[[ZERO2]], {{.*}}) : (!llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr, i64, !llvm.ptr) -> i32
// NATIVE: llvm.call @talos_rt_v1_covergroup_instance_query({{.*}}) : (!llvm.ptr, i64, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> f64
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> i32
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> i32
// NATIVE: llvm.call @talos_rt_v1_covergroup_type_query({{.*}}) : (!llvm.ptr, i64, i64, !llvm.ptr, !llvm.ptr, !llvm.ptr) -> i32
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> f64
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> i32
// NATIVE: llvm.load {{.*}} : !llvm.ptr -> i32

// BYTECODE-DAG: intrinsic {{.*}}id=0x00010450 inputs=3 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010454 inputs=2 outputs=3 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010455 inputs=2 outputs=3 flags=0

module empty_group;
  covergroup empty;
  endgroup
  empty cov;
  real instance_score, type_score;
  int instance_covered, instance_total, type_covered, type_total;
  initial begin
    cov = new();
    instance_score = cov.get_inst_coverage(instance_covered, instance_total);
    type_score = empty::get_coverage(type_covered, type_total);
    $display("%f %0d %0d %f %0d %0d", instance_score, instance_covered,
             instance_total, type_score, type_covered, type_total);
  end
endmodule
