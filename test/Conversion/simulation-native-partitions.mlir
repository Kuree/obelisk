// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-plan-native-partitions \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=LOWERED

// Native partition identity follows semantic ownership, not source order.
// The mutually recursive ordinary functions remain indivisible, both class
// methods share their declaring-class partition, and the root/metadata owner
// records explicit imports into every generated object partition.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @partitions {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.a"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.b"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "top.C.first"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "top.C.second"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "top.leaf"

    obelisk_sim.class.decl @C id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @C_first of @C implemented_by @class_first :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = false
    }
    obelisk_sim.class.method @C_second of @C implemented_by @class_second :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = false
    }

    obelisk_sim.func private @leaf(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func private @b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.call @a(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func private @class_second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.call @leaf(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.call @a(%ctx) : (!obelisk_sim.context) -> ()
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      obelisk_sim.class.direct_call @class_first %object() :
        (!obelisk_sim.class_handle<@C>) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func private @class_first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.call @leaf(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
    obelisk_sim.func private @a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.call @b(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.design @partitions attributes {
// CHECK-SAME: obelisk.native.partition_manifest = [
// CHECK-SAME: {dependencies = ["class:C", "scc:6:unit:26:unit:3", "unit:6"], exports = [], id = "primary", imports = [@a, @b, @class_first, @class_second, @leaf], members = [@root], owners = ["primary"]}
// CHECK-SAME: {dependencies = ["unit:6"], exports = [@class_first, @class_second], id = "class:C", imports = [@leaf], members = [@class_first, @class_second], owners = ["class:C"]}
// CHECK-SAME: {dependencies = [], exports = [@a, @b], id = "scc:6:unit:26:unit:3", imports = [], members = [@a, @b], owners = ["unit:2", "unit:3"]}
// CHECK-SAME: {dependencies = [], exports = [@leaf], id = "unit:6", imports = [], members = [@leaf], owners = ["unit:6"]}
// CHECK-SAME: ]}
// CHECK: obelisk_sim.func private @leaf
// CHECK-SAME: obelisk.native.partition = "unit:6"
// CHECK: obelisk_sim.func private @b
// CHECK-SAME: obelisk.native.partition = "scc:6:unit:26:unit:3"
// CHECK: obelisk_sim.func private @class_second
// CHECK-SAME: obelisk.native.partition = "class:C"
// CHECK: obelisk_sim.func @root
// CHECK-SAME: obelisk.native.partition = "primary"
// CHECK: obelisk_sim.func private @class_first
// CHECK-SAME: obelisk.native.partition = "class:C"
// CHECK: obelisk_sim.func private @a
// CHECK-SAME: obelisk.native.partition = "scc:6:unit:26:unit:3"

// Partition identity survives function replacement and outlining. Generated
// process helpers remain with their semantic owner rather than falling into a
// worker-dependent catch-all partition.
// LOWERED: module attributes {
// LOWERED-SAME: obelisk.native.partition_manifests = [{design = "partitions", partitions = [
// LOWERED-SAME: {dependencies = ["class:C", "scc:6:unit:26:unit:3", "unit:6"], exports = [], id = "primary", imports = [@a, @b, @class_first, @class_second, @leaf], members = [@root], owners = ["primary"]}
// LOWERED-SAME: ]}
// LOWERED-SAME: obelisk.native.physical_partition_manifest = [
// LOWERED-SAME: {dependencies = ["primary", "unit:6"], exports = [@class_first], id = "class:C", imports = [@__obelisk_current_context, @leaf], members = [@class_first, @class_second]}
// LOWERED-SAME: {dependencies = [], exports = [@leaf], id = "unit:6", imports = [], members = [@leaf]}
// LOWERED: llvm.func @leaf
// LOWERED-SAME: obelisk.native.partition = "unit:6"
// LOWERED: llvm.func @b
// LOWERED-SAME: obelisk.native.partition = "scc:6:unit:26:unit:3"
// LOWERED: llvm.func @class_second
// LOWERED-SAME: obelisk.native.partition = "class:C"
// LOWERED: llvm.func @root
// LOWERED-SAME: obelisk.native.partition = "primary"
// LOWERED: llvm.func @root.__obelisk_native_requirements
// LOWERED-SAME: obelisk.native.partition = "primary"
// LOWERED: llvm.func @root.__obelisk_native_execute
// LOWERED-SAME: obelisk.native.partition = "primary"
// LOWERED: llvm.func @root.__obelisk_native_destroy
// LOWERED-SAME: obelisk.native.partition = "primary"
// LOWERED: llvm.func @root.__obelisk_spawn
// LOWERED-SAME: obelisk.native.partition = "primary"
// LOWERED: llvm.func @class_first
// LOWERED-SAME: obelisk.native.partition = "class:C"
// LOWERED: llvm.func @a
// LOWERED-SAME: obelisk.native.partition = "scc:6:unit:26:unit:3"
