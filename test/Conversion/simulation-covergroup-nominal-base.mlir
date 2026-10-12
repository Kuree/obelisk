// RUN: %obelisk --std=1800-2023 --coverage=functional -emit-sim %S/../Coverage/functional-covergroup-formals-inherited.sv | obelisk-opt '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %t.mlir --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.decl match={sym_name = "__obelisk_covergroup_observer_2DB884449AC9C6C6"} set={base = @__obelisk_covergroup_s5_37DD88C459488151}' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=BASE
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.decl match={sym_name = "__obelisk_covergroup_observer_2DB884449AC9C6C6"} set={base = @__obelisk_covergroup_s5_37DD88C459488151}' --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BASE

// IEEE 1800-2023 19.4.1: `covergroup extends` records the base covergroup as
// the declaration's ancestor, a derived instance is cast to the base handle
// type, and the formal keeps its static base type.
// CHECK: simulation.covergroup.decl @[[ROOT:__obelisk_covergroup_s5_[0-9A-F]+]] schema
// CHECK-NEXT: simulation.covergroup.decl @[[DERIVED:__obelisk_covergroup_s48_[0-9A-F]+]] schema {{[0-9]+}} base @[[ROOT]]
// CHECK: simulation.covergroup.decl @[[OBSERVER:__obelisk_covergroup_observer_[0-9A-F]+]] schema {{[0-9]+}} debug "observer"
// CHECK: simulation.covergroup.cast {{.*}} : !simulation.covergroup_handle<@[[DERIVED]]> to !simulation.covergroup_handle<@[[ROOT]]>
// CHECK: simulation.covergroup.create {{.*}} from @[[OBSERVER]] payloads[{{.*}}] argument_count 1 {{.*}} : (!simulation.covergroup_handle<@[[ROOT]]>,
// CHECK: simulation.covergroup.formal_read {{.*}} -> !simulation.covergroup_handle<@[[ROOT]]>

// The semantic declaration and the schema's nominal hierarchy must agree
// before either backend accepts a covergroup handle: forging a base on the
// unrelated observer declaration is rejected by the shared verifier.
// BASE: covergroup base disagrees with the embedded schema
