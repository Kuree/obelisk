// RUN: %talos --std=1800-2023 -emit-talos %S/talos-to-simulation-covergroup-virtual-interface-formals.sv | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from="!simulation.virtual_interface<\"@s3.$root::@s6.top::@s7.first\", \"\">" replace-to=i64' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from="!simulation.virtual_interface<\"@s3.$root::@s6.top::@s7.first\", \"\">" replace-to="!simulation.virtual_interface<\"@s3.$root::@s6.top::@s7.first\", \"view\">"' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.formal_read clone=true replace-from="!simulation.virtual_interface<\"@s3.$root::@s6.top::@s7.first\", \"\">" replace-to="!simulation.virtual_interface<\"@s3.$root::@s6.top::@s19.other\", \"\">"' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.create operand=1 constant={0 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=CREATE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.sample operand=2 constant={0 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=SAMPLE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.scope.decl match={hierarchical_name = \"top.unused\"} set={interface_type = \"@s3.$root::@s6.top::@s19.other\"}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCOPE

// Companion of talos-to-simulation-covergroup-virtual-interface-formals.sv.
// IEEE 1800-2023 25.9 and 19.8.1: a virtual interface is not an equal-width
// scalar and its specialization and modport are part of the type. Reading a
// formal as i64, as the "view" modport, or as another specialization fails the
// FunctionalFormal type check shared by the LLVM lowering and the bytecode
// encoder.
// TYPE: type-mismatched constructor FunctionalFormal ID

// A constructor payload that is an i64 instead of the virtual-interface value
// and a sample value that is an i64 instead of the bus.view default are
// rejected by the same schema verifier.
// CREATE: constructor FunctionalFormal batch is reordered, wrong-owner, or type-mismatched at schema ordinal 0
// SAMPLE: type-mismatched sample FunctionalExpression ID

// IEEE 1800-2023 25.9: the schema's scope-to-specialization bindings must
// agree with the elaborated interface instances; binding top.unused to the
// WIDTH=4 specialization is rejected.
// SCOPE: functional virtual interface bindings disagree with elaborated interface scopes
