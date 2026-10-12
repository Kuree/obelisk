// RUN: obelisk-opt %s --split-input-file --test-obelisk-class-dispatch-analysis -o /dev/null 2>&1 | FileCheck %s

// IEEE 8.13, 8.14 and 8.20: an 8-deep chain shares C0's three method entries.
// The 16-node budget fails if any class copies its inherited table, the
// slot-5 override changes the effective target below C7, and the sparse slot
// and signature exercise the full key space. Declaration order is reversed.
// CHECK: bounded-dispatch-metadata classes=8
// CHECK-NEXT: dispatch C7 -> C0 is-instance=1 resolved=C0_low compatible=8 implementations=[C0_low]
// CHECK-NEXT: dispatch C7 -> C6 is-instance=1 resolved=C0_high compatible=2 implementations=[C0_high]
// CHECK-NEXT: dispatch C7 -> C7 is-instance=1 resolved=C0_other compatible=1 implementations=[C0_other]
// CHECK-NEXT: dispatch C0 -> C7 is-instance=0 resolved=C0_low compatible=1 implementations=[C0_low]
// CHECK-NEXT: dispatch C7 -> C0 is-instance=1 resolved=C0_low compatible=8 implementations=[]

module attributes {
  test.dispatch_queries = [
    {dynamic = @C7, target = @C0, slot = 4294967295 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @C7, target = @C6, slot = 5 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @C7, target = @C7, slot = 1048576 : i64, signature = 4611686018427387905 : i64, is_task = false},
    {dynamic = @C0, target = @C7, slot = 0 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @C7, target = @C0, slot = 0 : i64, signature = 17 : i64, is_task = true}
  ],
  test.dispatch_table_node_limit = 16 : i64
} {
  simulation.design @dispatch {
    simulation.scope.decl 0
    simulation.class.decl @C7 id 8 extends @C6 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C6 id 7 extends @C5 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C5 id 6 extends @C4 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C4 id 5 extends @C3 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C3 id 4 extends @C2 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C2 id 3 extends @C1 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C1 id 2 extends @C0 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @C0 id 1 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.method @C0_low of @C0 slot 0 signature_id 17 implemented_by @C0_low_impl : (!simulation.context, !simulation.class_handle<@C0>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @C0_high of @C0 slot 5 signature_id 17 implemented_by @C0_high_impl : (!simulation.context, !simulation.class_handle<@C0>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @C0_other of @C0 slot 1048576 signature_id 4611686018427387905 implemented_by @C0_other_impl : (!simulation.context, !simulation.class_handle<@C0>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 1 in 0 function hierarchy "C0_low"
    simulation.func private @C0_low_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C0> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "C0_high"
    simulation.func private @C0_high_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C0> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "C0_other"
    simulation.func private @C0_other_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@C0> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
  }
}

// -----

// IEEE 8.26: the IRoot diamond through ILeft and IRight does not duplicate
// compatible concrete classes or implementation targets. Task queries against
// functions and unknown signatures find no implementation.
// CHECK-NEXT: dispatch Child -> IRoot is-instance=1 resolved=Base_low compatible=2 implementations=[Base_low, Override_low]
// CHECK-NEXT: dispatch Override -> IJoin is-instance=1 resolved=Override_low compatible=2 implementations=[Base_low, Override_low]
// CHECK-NEXT: dispatch Child -> IRoot is-instance=1 resolved=Base_other compatible=2 implementations=[Base_other]
// CHECK-NEXT: dispatch Child -> Override is-instance=0 resolved=Base_low compatible=1 implementations=[Override_low]
// CHECK-NEXT: dispatch Unrelated -> ILeft is-instance=0 resolved=none compatible=2 implementations=[Base_low, Override_low]
// CHECK-NEXT: dispatch Child -> IRoot is-instance=1 resolved=Base_low compatible=2 implementations=[]
// CHECK-NEXT: dispatch Child -> IRoot is-instance=1 resolved=none compatible=2 implementations=[]
// CHECK-NEXT: dispatch Child -> Base is-instance=1 resolved=Base_high compatible=2 implementations=[Base_high]

module attributes {
  test.dispatch_queries = [
    {dynamic = @Child, target = @IRoot, slot = 4294967295 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @Override, target = @IJoin, slot = 4294967295 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @Child, target = @IRoot, slot = 4294967295 : i64, signature = 23 : i64, is_task = false},
    {dynamic = @Child, target = @Override, slot = 0 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @Unrelated, target = @ILeft, slot = 4294967295 : i64, signature = 17 : i64, is_task = false},
    {dynamic = @Child, target = @IRoot, slot = 4294967295 : i64, signature = 17 : i64, is_task = true},
    {dynamic = @Child, target = @IRoot, slot = 4294967295 : i64, signature = 101 : i64, is_task = false},
    {dynamic = @Child, target = @Base, slot = 5 : i64, signature = 17 : i64, is_task = false}
  ]
} {
  simulation.design @dispatch {
    simulation.scope.decl 0
    simulation.class.decl @Unrelated id 8 {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @Override id 7 extends @Base {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @Child id 6 extends @Base {is_abstract = false, is_final = false, is_interface = false}
    simulation.class.decl @Base id 5 implements [@IJoin] {is_abstract = true, is_final = false, is_interface = false}
    simulation.class.decl @IJoin id 4 implements [@ILeft, @IRight] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.decl @IRight id 3 implements [@IRoot] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.decl @ILeft id 2 implements [@IRoot] {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.decl @IRoot id 1 {is_abstract = true, is_final = false, is_interface = true}
    simulation.class.method @IRoot_low of @IRoot slot 4294967295 signature_id 17 interface_ordinal 0 : (!simulation.context, !simulation.class_handle<@IRoot>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @IRoot_other of @IRoot slot 4294967295 signature_id 23 interface_ordinal 1 : (!simulation.context, !simulation.class_handle<@IRoot>) -> i64 {is_final = false, is_pure = true, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Base_low of @Base slot 0 signature_id 17 implemented_by @Base_low_impl : (!simulation.context, !simulation.class_handle<@Base>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Base_high of @Base slot 5 signature_id 17 implemented_by @Base_high_impl : (!simulation.context, !simulation.class_handle<@Base>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Base_other of @Base slot 1 signature_id 23 implemented_by @Base_other_impl : (!simulation.context, !simulation.class_handle<@Base>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Override_low of @Override slot 0 signature_id 17 implemented_by @Override_low_impl : (!simulation.context, !simulation.class_handle<@Override>) -> i64 {is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.code_unit.decl 3 in 0 function hierarchy "Base_low"
    simulation.func private @Base_low_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "Base_high"
    simulation.func private @Base_high_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
    simulation.code_unit.decl 5 in 0 function hierarchy "Base_other"
    simulation.func private @Base_other_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
    simulation.code_unit.decl 6 in 0 function hierarchy "Override_low"
    simulation.func private @Override_low_impl(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Override> {simulation.capture_kind = 1 : i32}) -> i64 attributes {code_unit_id = 6 : i64, entry_kind = 8 : i32} {
      %v = arith.constant 1 : i64
      simulation.return %v : i64
    }
  }
}
