// RUN: talos-opt %s --talos-sim-plan-native-partitions | FileCheck %s
// RUN: talos-opt %s --talos-sim-plan-native-partitions \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines \
// RUN:   -o %t.parallel
// RUN: FileCheck %s --check-prefix=LOWERED < %t.parallel
// RUN: talos-opt %s --mlir-disable-threading --talos-sim-plan-native-partitions \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines -o %t.serial
// RUN: diff %t.parallel %t.serial

// Native partition identity follows semantic ownership, not source order.
// The mutually recursive ordinary functions remain one semantic SCC, each
// class method follows its stable code-unit owner, and the root/metadata owner
// records explicit imports into every generated semantic partition.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @partitions {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.a"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.b"
    simulation.code_unit.decl 4 in 0 function hierarchy "top.C.first"
    simulation.code_unit.decl 5 in 0 function hierarchy "top.C.second"
    simulation.code_unit.decl 6 in 0 function hierarchy "top.leaf"

    simulation.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @C_first of @C implemented_by @class_first :
      (!simulation.context, !simulation.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = false
    }
    simulation.class.method @C_second of @C implemented_by @class_second :
      (!simulation.context, !simulation.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = false
    }

    simulation.func private @leaf(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      simulation.return
    }
    simulation.func private @b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      simulation.call @a(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @class_second(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      simulation.return
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      simulation.call @leaf(%ctx) : (!simulation.context) -> ()
      simulation.call @a(%ctx) : (!simulation.context) -> ()
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      simulation.class.direct_call @class_first %object() :
        (!simulation.class_handle<@C>) -> ()
      simulation.return
    }
    simulation.func private @class_first(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.call @leaf(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.call @b(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
  }
}

// CHECK: simulation.design @partitions attributes {
// CHECK-SAME: native.partition_manifest = [
// CHECK-SAME: #native.partition<id = "primary", owners = ["primary"], members = [@root], imports = [@a, @b, @class_first, @class_second, @leaf], exports = [], dependencies = ["scc:6:unit:26:unit:3", "unit:4", "unit:5", "unit:6"]>
// CHECK-SAME: #native.partition<id = "scc:6:unit:26:unit:3", owners = ["unit:2", "unit:3"], members = [@a, @b], imports = [], exports = [@a, @b], dependencies = []>
// CHECK-SAME: #native.partition<id = "unit:4", owners = ["unit:4"], members = [@class_first], imports = [@leaf], exports = [@class_first], dependencies = ["unit:6"]>
// CHECK-SAME: #native.partition<id = "unit:5", owners = ["unit:5"], members = [@class_second], imports = [], exports = [@class_second], dependencies = []>
// CHECK-SAME: #native.partition<id = "unit:6", owners = ["unit:6"], members = [@leaf], imports = [], exports = [@leaf], dependencies = []>
// CHECK-SAME: ]}
// CHECK: simulation.func private @leaf
// CHECK-SAME: native.partition = "unit:6"
// CHECK: simulation.func private @b
// CHECK-SAME: native.partition = "scc:6:unit:26:unit:3"
// CHECK: simulation.func private @class_second
// CHECK-SAME: native.partition = "unit:5"
// CHECK: simulation.func @root
// CHECK-SAME: native.partition = "primary"
// CHECK: simulation.func private @class_first
// CHECK-SAME: native.partition = "unit:4"
// CHECK: simulation.func private @a
// CHECK-SAME: native.partition = "scc:6:unit:26:unit:3"

// Partition identity survives function replacement and outlining. Generated
// process helpers remain with their semantic owner rather than falling into a
// worker-dependent catch-all partition.
// LOWERED: module attributes {
// LOWERED-SAME: native.partition_manifests = [#native.design_partitions<design = "partitions", partitions = [
// LOWERED-SAME: #native.partition<id = "primary", owners = ["primary"], members = [@root], imports = [@a, @b, @class_first, @class_second, @leaf], exports = [], dependencies = ["scc:6:unit:26:unit:3", "unit:4", "unit:5", "unit:6"]>
// LOWERED-SAME: ]>]
// LOWERED-SAME: native.physical_partition_manifest = [
// LOWERED-SAME: #native.partition<id = "unit:4", members = [@class_first], imports = [@__talos_current_context, @leaf], exports = [@class_first], dependencies = ["primary", "unit:6"]>
// LOWERED-SAME: #native.partition<id = "unit:5", members = [@class_second], imports = [], exports = [], dependencies = []>
// LOWERED-SAME: #native.partition<id = "unit:6", members = [@leaf], imports = [], exports = [@leaf], dependencies = []>
// LOWERED: llvm.func @leaf
// LOWERED-SAME: native.partition = "unit:6"
// LOWERED: llvm.func @b
// LOWERED-SAME: native.partition = "scc:6:unit:26:unit:3"
// LOWERED: llvm.func @class_second
// LOWERED-SAME: native.partition = "unit:5"
// LOWERED: llvm.func @root
// LOWERED-SAME: native.partition = "primary"
// LOWERED: llvm.func @root.__talos_native_execute
// LOWERED-SAME: native.partition = "primary"
// LOWERED: llvm.func @root.__talos_spawn
// LOWERED-SAME: native.partition = "primary"
// LOWERED: llvm.func @class_first
// LOWERED-SAME: native.partition = "unit:4"
// LOWERED: llvm.func @a
// LOWERED-SAME: native.partition = "scc:6:unit:26:unit:3"
