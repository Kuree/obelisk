// RUN: %talos --std=1800-2023 -emit-talos %S/../Coverage/functional-aggregate-formals.sv | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: talos-opt %t.mlir --test-talos-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines \
// RUN:   | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.mlir --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=replace-from="\"number\"" replace-to="\"wrong\""' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=replace-from="\"number\"" replace-to="\"wrong\""' --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=INVALID

// Aggregate snapshots use traced typed storage and a path with no watcher.
// Reads borrow the complete aggregate type; individual fields and array
// elements remain typed views after CSE. Neither backend may erase the schema
// identity even when another aggregate has the same physical shape.
// CHECK-DAG: simulation.container.create
// CHECK-DAG: simulation.reference_path.index {{.*}} : (!simulation.context, !simulation.dynamic_array<{{.*}}>, i64) -> !simulation.reference_path<
// CHECK-DAG: simulation.covergroup.create
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.unpacked_struct<
// CHECK-DAG: simulation.reference_path.aggregate_element
// CHECK-DAG: simulation.reference_path.aggregate_slice
// SCHEMA-COUNT-9: functional_formal {{.*}} result_kind=9
// SCHEMA-COUNT-3: functional_aggregate_type
// INVALID: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID

// Renaming the struct field "number" everywhere keeps size, alignment, states
// and offsets equal but gives the aggregate a distinct SystemVerilog type and
// schema identity, which both backends must reject on the constructor reads.
