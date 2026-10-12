// RUN: %obelisk --std=1800-2023 -emit-obelisk %S/../Coverage/functional-class-formals.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --canonicalize --cse | FileCheck %s
// RUN: obelisk-opt %t.mlir --test-obelisk-print-coverage-schema -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.formal_read replace-from=!simulation.class_handle<@__obelisk_class_s56_Unallocated> replace-to=!simulation.class_handle<@__obelisk_class_s18_Record>' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not obelisk-opt %t.mlir --verify-each=0 '--test-obelisk-edit-attributes=op=simulation.class.decl match={sym_name = "__obelisk_class_s18_Record"} set={id = 2 : i64}' '--test-obelisk-edit-attributes=op=simulation.class.decl match={sym_name = "__obelisk_class_s3_Readable"} set={id = 3 : i64}' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=IDENTITY

// IEEE 1800-2023 19.3: class-handle formals lower to typed nullable handles
// and ref formals to typed argument references, and survive canonicalization.
// CHECK-DAG: simulation.covergroup.create
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.class_handle<
// CHECK-DAG: simulation.covergroup.formal_read {{.*}} -> !simulation.argument_ref<!simulation.class_handle<
// CHECK-DAG: simulation.argument_ref.load {{.*}} -> !simulation.class_handle<
// CHECK-DAG: simulation.argument_ref.store {{.*}} : !simulation.class_handle<

// The shared schema has eleven handle-valued formals (result_kind 10, no
// width) over three distinct nominal class layouts.
// SCHEMA-COUNT-11: functional_formal {{.*}} result_kind=10 {{.*}} width=0
// SCHEMA-COUNT-3: functional_class_type
// SCHEMA: functional_expression {{.*}} role=17 result_kind=10 width=0

// Equal 64-bit representations do not let one nominal class stand in for
// another: reading the null-only Unallocated formal as a Record handle is
// rejected against the schema's class type for that formal.
// TYPE: has a nonexistent, wrong-owner, or type-mismatched constructor FunctionalFormal ID

// Formal identities follow class identity: swapping the Record and Readable
// class IDs keeps the design dense but no longer matches the schema's class
// types. The two edits run unverified because the first leaves a duplicate.
// IDENTITY: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
