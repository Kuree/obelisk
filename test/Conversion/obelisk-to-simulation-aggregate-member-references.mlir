// RUN: %obelisk --std=1800-2023 -emit-obelisk %S/../Coverage/functional-aggregate-member-references.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// References to aggregate members keep a typed parent reference, with a fixed
// offset and the parent's plane geometry. The selected descriptor owns only
// its field's trace metadata. Primitive fields and container handles share the
// same projection operation before either backend.
// CHECK-DAG: simulation.reference_path.aggregate_slice {{.*}} -> !simulation.reference_path<i32>
// CHECK-DAG: simulation.reference_path.aggregate_slice {{.*}} -> !simulation.reference_path<!simulation.packed_array<6 : 0 x i1>>
// CHECK-DAG: simulation.reference_path.aggregate_slice {{.*}} -> !simulation.reference_path<!simulation.packed_array<2 : 0 x !simulation.logic<1>>>
// CHECK-DAG: simulation.reference_path.aggregate_slice {{.*}} -> !simulation.reference_path<f64>
// CHECK-DAG: simulation.reference_path.aggregate_slice {{.*}} -> !simulation.reference_path<!simulation.string>
// CHECK-DAG: simulation.reference_path.aggregate_slice {{.*}} -> !simulation.reference_path<!simulation.dynamic_array<i32>>
