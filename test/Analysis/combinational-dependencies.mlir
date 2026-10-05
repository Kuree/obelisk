// RUN: obelisk-opt %s --test-obelisk-combinational-dependencies -o /dev/null 2> %t.threaded
// RUN: obelisk-opt %s --test-obelisk-combinational-dependencies --mlir-disable-threading -o /dev/null 2> %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: FileCheck %s < %t.threaded

// The actor graph is cyclic (LVT -> bank -> LVT); the memory-statement graph
// is a DAG. Address publication precedes result consumption in source order.
// A clocked reader remains an observer even though it watches a different root.
module {
  simulation.design @address_then_result {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "lvt"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "bank"
    simulation.code_unit.decl 3 in 0 always hierarchy "sample"
    simulation.func @lvt(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %address = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %bank_address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %bank_result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      simulation.ref.store %a to %bank_address : i8, !simulation.ref<i8>
      %r = simulation.ref.load %bank_result : !simulation.ref<i8> -> i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.any %address, %bank_result edges [0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i8>
    }
    simulation.func @bank(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %address = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %result = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      %a = simulation.ref.load %address : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %r = arith.addi %a, %one : i8
      simulation.ref.store %r to %result : i8, !simulation.ref<i8>
      simulation.suspend.change %address to ^body : !simulation.ref<i8>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %result = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<i8>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<i1>
    ^body:
      %r = simulation.ref.load %result : !simulation.ref<i8> -> i8
      simulation.nba.enqueue %r to %sample : (i8, !simulation.ref<i8>) -> ()
      cf.br ^wait
    }
  }
// CHECK-LABEL: dependencies @address_then_result unknown=0
// CHECK: summary @lvt opaque=0 read:s0[0,8) read:s2[0,8) write:s1[0,8) write:s3[0,8)
// CHECK: summary @sample opaque=0 read:s3[0,8) nba:s5[0,8) watch:s4[0,1)
// CHECK: node 2 @lvt simulation.ref.store opaque=0 write:s1[0,8)
// CHECK: node 3 @lvt simulation.ref.load opaque=0 read:s2[0,8)
// CHECK: edge 2 -> 3 order
// CHECK: edge 2 -> 7 memory
// CHECK: edge 8 -> 3 memory
// CHECK-NOT: cyclic=1
// CHECK: observer @sample read:s3[0,8) nba:s5[0,8) watch:s4[0,1)

  // Calls are contextually instantiated, including offsets into actual views.
  // A summary is not the Cartesian product of every design read and write.
  simulation.design @calls {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i16 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "leaf"
    simulation.code_unit.decl 2 in 0 function hierarchy "middle"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "caller"
    simulation.func private @leaf(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func private @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.call @leaf(%ctx, %in, %out) : (!simulation.context, !simulation.ref<i8>, !simulation.ref<i8>) -> ()
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %hi = simulation.ref.extract %in from 8 : !simulation.ref<i16> -> !simulation.ref<i8>
      cf.br ^body
    ^body:
      simulation.call @middle(%ctx, %hi, %out) : (!simulation.context, !simulation.ref<i8>, !simulation.ref<i8>) -> ()
      simulation.suspend.change %in to ^body : !simulation.ref<i16>
    }
  }
// CHECK-LABEL: dependencies @calls unknown=0
// CHECK: summary @leaf opaque=0 read:a1[0,8) write:a2[0,8)
// CHECK: summary @middle opaque=0 read:a1[0,8) write:a2[0,8)
// CHECK: summary @caller opaque=0 read:s0[8,16) write:s1[0,8) watch:s0[0,16)
// CHECK: simulation.call opaque=0 read:s0[8,16) write:s1[0,8)

  // A formal slice of a reference phi includes every actual view. The phi
  // joins two fixed offsets: dynamic=false does not imply one fixed offset.
  simulation.design @reference_phi {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i4 design
    simulation.code_unit.decl 1 in 0 function hierarchy "slice"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "caller"
    simulation.func private @slice(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %in: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}, %out: !simulation.ref<i4> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %view = simulation.ref.extract %in from 2 : !simulation.ref<i8> -> !simulation.ref<i4>
      %v = simulation.ref.load %view : !simulation.ref<i4> -> i4
      simulation.ref.store %v to %out : i4, !simulation.ref<i4>
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %choose: i1 {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i4>
      %low = simulation.ref.extract %root from 0 : !simulation.ref<i64> -> !simulation.ref<i8>
      %high = simulation.ref.extract %root from 56 : !simulation.ref<i64> -> !simulation.ref<i8>
      cf.cond_br %choose, ^join(%low : !simulation.ref<i8>), ^join(%high : !simulation.ref<i8>)
    ^join(%view: !simulation.ref<i8>):
      simulation.call @slice(%ctx, %view, %out) : (!simulation.context, !simulation.ref<i8>, !simulation.ref<i4>) -> ()
      simulation.suspend.change %root to ^join(%view : !simulation.ref<i8>) : !simulation.ref<i64>
    }
  }
// CHECK-LABEL: dependencies @reference_phi unknown=0
// CHECK: summary @slice opaque=0 read:a1[2,6) write:a2[0,4)
// CHECK: summary @caller opaque=0 read:s0[2,62) write:s1[0,4) watch:s0[0,64)
// CHECK: simulation.call opaque=0 read:s0[2,62) write:s1[0,4)

  // Recursive calls have unknown transitive effects. A sparse call-graph
  // fixed point must terminate and never mistake the recursion for no effects.
  simulation.design @recursion {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 function hierarchy "first"
    simulation.code_unit.decl 2 in 0 function hierarchy "second"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "caller"
    simulation.func private @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      simulation.call @second(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.call @first(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func @caller(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      cf.br ^body
    ^body:
      simulation.call @first(%ctx) : (!simulation.context) -> ()
      simulation.suspend.change %root to ^body : !simulation.ref<i8>
    }
  }
// CHECK-LABEL: dependencies @recursion unknown=1
// CHECK: summary @first opaque=1
// CHECK: summary @second opaque=1
// CHECK: summary @caller opaque=1 watch:s0[0,8)
// CHECK: simulation.call opaque=1
}
