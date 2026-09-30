// RUN: obelisk-opt %s --test-obelisk-storage-writes -o /dev/null 2>&1 | FileCheck %s
// LRM 4.4.2.4, 4.6, 10.4.2, 11.5.1: loops, path joins, storage aliases,
// and a fresh NBA window are facts of control/data flow, not syntax rules.
// The printed lattice facts are internal certificates, not LRM requirements.
// #0 runs before the NBA drain; an event wait can resume in the same time slot.
// Capture root metadata identifies storage but does not prove a fixed address.
!array = !simulation.unpacked_array<0 : 3 x i16>
module {
  simulation.design @writes {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "diamond"
    simulation.code_unit.decl 2 in 0 function hierarchy "structured"
    simulation.code_unit.decl 3 in 0 function hierarchy "sequential"
    simulation.code_unit.decl 4 in 0 function hierarchy "join_root"
    simulation.code_unit.decl 5 in 0 function hierarchy "join_alias"
    simulation.code_unit.decl 6 in 0 function hierarchy "zero_time_loop"
    simulation.code_unit.decl 7 in 0 always hierarchy "positive_delay"
    simulation.code_unit.decl 8 in 0 always hierarchy "zero_delay"
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i64 design
    simulation.storage.decl 2 in 0 : !array design
    simulation.code_unit.decl 9 in 0 function hierarchy "array_lanes"
    simulation.code_unit.decl 10 in 0 always hierarchy "event_wait"
    simulation.code_unit.decl 11 in 0 function hierarchy "join_view"
    simulation.code_unit.decl 12 in 0 function hierarchy "region_handle"
    simulation.code_unit.decl 13 in 0 function hierarchy "loop_handle"
    simulation.code_unit.decl 14 in 0 function hierarchy "capture_handle"
    simulation.code_unit.decl 15 in 0 function hierarchy "join_nested_view"
    simulation.func @diamond(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}, %index: i32 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i8
      cf.cond_br %condition, ^left, ^right
    ^left:
      %a = simulation.ref.dyn_extract %root from %index : (!simulation.ref<i64>, i32) -> !simulation.ref<i8>
      simulation.ref.store %one to %a : i8, !simulation.ref<i8>
      cf.br ^exit
    ^right:
      %b = simulation.ref.dyn_extract %root from %index : (!simulation.ref<i64>, i32) -> !simulation.ref<i8>
      simulation.ref.store %one to %b : i8, !simulation.ref<i8>
      cf.br ^exit
    ^exit:
      simulation.return
    }
    simulation.func @structured(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      scf.if %condition {
        simulation.ref.store %one to %root : i64, !simulation.ref<i64>
      } else {
        simulation.ref.store %one to %root : i64, !simulation.ref<i64>
      }
      simulation.return
    }
    simulation.func @sequential(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      simulation.ref.store %one to %root : i64, !simulation.ref<i64>
      simulation.ref.store %one to %root : i64, !simulation.ref<i64>
      simulation.return
    }
    simulation.func @join_root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}, %index: i32 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      cf.cond_br %condition, ^join(%root : !simulation.ref<i64>), ^forward
    ^forward:
      cf.br ^join(%root : !simulation.ref<i64>)
    ^join(%same: !simulation.ref<i64>):
      %a = simulation.ref.dyn_extract %same from %index : (!simulation.ref<i64>, i32) -> !simulation.ref<i8>
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %a : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func @join_alias(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %other = simulation.context.storage %ctx[1] : !simulation.ref<i64>
      cf.cond_br %condition, ^join(%root : !simulation.ref<i64>), ^join(%other : !simulation.ref<i64>)
    ^join(%unknown: !simulation.ref<i64>):
      %one = arith.constant 1 : i64
      simulation.ref.store %one to %unknown : i64, !simulation.ref<i64>
      simulation.return
    }
    simulation.func @zero_time_loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      cf.br ^loop
    ^loop:
      simulation.ref.store %one to %root : i64, !simulation.ref<i64>
      cf.br ^loop
    }
    simulation.func @positive_delay(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 7 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      %delay = simulation.time.constant 1
      cf.br ^loop
    ^loop:
      simulation.nba.enqueue %one to %root : (i64, !simulation.ref<i64>) -> ()
      simulation.suspend.delay %delay to ^loop
    }
    simulation.func @zero_delay(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 8 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      %delay = simulation.time.constant 0
      cf.br ^loop
    ^loop:
      simulation.nba.enqueue %one to %root : (i64, !simulation.ref<i64>) -> ()
      simulation.suspend.delay %delay to ^loop
    }
    simulation.func @array_lanes(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %index: i32 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %root = simulation.context.storage %ctx[2] : !simulation.ref<!array>
      %element = simulation.ref.array_element %root[%index] : (!simulation.ref<!array>, i32) -> !simulation.ref<i16>
      %high = simulation.ref.extract %element from 8 : !simulation.ref<i16> -> !simulation.ref<i8>
      %fixed = simulation.ref.subelement %root[[2]] : !simulation.ref<!array> -> !simulation.ref<i16>
      %one = arith.constant 1 : i8
      %two = arith.constant 2 : i16
      simulation.ref.store %one to %high : i8, !simulation.ref<i8>
      simulation.ref.store %two to %fixed : i16, !simulation.ref<i16>
      simulation.return
    }
    simulation.func @event_wait(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 10 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      cf.br ^loop
    ^loop:
      simulation.nba.enqueue %one to %root : (i64, !simulation.ref<i64>) -> ()
      simulation.suspend.change %root to ^loop : !simulation.ref<i64>
    }
    simulation.func @join_view(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %low = simulation.ref.extract %root from 0 : !simulation.ref<i64> -> !simulation.ref<i8>
      %high = simulation.ref.extract %root from 56 : !simulation.ref<i64> -> !simulation.ref<i8>
      cf.cond_br %condition, ^join(%low : !simulation.ref<i8>), ^join(%high : !simulation.ref<i8>)
    ^join(%view: !simulation.ref<i8>):
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %view : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func @region_handle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %a = simulation.ref.extract %root from 8 : !simulation.ref<i64> -> !simulation.ref<i8>
      %b = simulation.ref.extract %root from 8 : !simulation.ref<i64> -> !simulation.ref<i8>
      %selected = scf.if %condition -> (!simulation.ref<i8>) {
        scf.yield %a : !simulation.ref<i8>
      } else {
        scf.yield %b : !simulation.ref<i8>
      }
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %selected : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func @loop_handle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %one = arith.constant 1 : i64
      cf.br ^loop(%root : !simulation.ref<i64>)
    ^loop(%carried: !simulation.ref<i64>):
      simulation.ref.store %one to %carried : i64, !simulation.ref<i64>
      cf.cond_br %condition, ^loop(%carried : !simulation.ref<i64>), ^exit
    ^exit:
      simulation.return
    }
    simulation.func @capture_handle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %capture: !simulation.ref<i64> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %one = arith.constant 1 : i64
      simulation.ref.store %one to %capture : i64, !simulation.ref<i64>
      simulation.return
    }
    simulation.func @join_nested_view(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      %root = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %low = simulation.ref.extract %root from 0 : !simulation.ref<i64> -> !simulation.ref<i8>
      %high = simulation.ref.extract %root from 56 : !simulation.ref<i64> -> !simulation.ref<i8>
      cf.cond_br %condition, ^join(%low : !simulation.ref<i8>), ^join(%high : !simulation.ref<i8>)
    ^join(%view: !simulation.ref<i8>):
      %selected = simulation.ref.extract %view from 2 : !simulation.ref<i8> -> !simulation.ref<i4>
      %one = arith.constant 1 : i4
      simulation.ref.store %one to %selected : i4, !simulation.ref<i4>
      simulation.return
    }
  }
}
// CHECK-LABEL: function diamond
// CHECK-NEXT: write 0 once=1 root=0 bits=64 lane=0:8 stride=1 dynamic=1 clipped=1 fixed=0 direct=1
// CHECK-NEXT: write 1 once=1 root=0 bits=64 lane=0:8 stride=1 dynamic=1 clipped=1 fixed=0 direct=1
// CHECK-NEXT: pair 0,1 exclusive=1
// CHECK-LABEL: function structured
// CHECK-NEXT: write 0 once=1 root=0 bits=64 lane=0:64 stride=0 dynamic=0 clipped=0
// CHECK-NEXT: write 1 once=1 root=0 bits=64 lane=0:64 stride=0 dynamic=0 clipped=0
// CHECK-NEXT: pair 0,1 exclusive=1
// CHECK-LABEL: function sequential
// CHECK: pair 0,1 exclusive=0
// CHECK-LABEL: function join_root
// CHECK-NEXT: write 0 once=1 root=0 bits=64 lane=0:8 stride=1 dynamic=1 clipped=1
// CHECK-LABEL: function join_alias
// CHECK-NEXT: write 0 once=1 unknown
// CHECK-LABEL: function zero_time_loop
// CHECK-NEXT: write 0 once=0 root=0 bits=64
// CHECK-LABEL: function positive_delay
// CHECK-NEXT: write 0 once=1 root=0 bits=64
// CHECK-LABEL: function zero_delay
// CHECK-NEXT: write 0 once=0 root=0 bits=64
// CHECK-LABEL: function array_lanes
// CHECK-NEXT: write 0 once=1 root=2 bits=64 lane=8:8 stride=16 dynamic=1 clipped=0
// CHECK-NEXT: write 1 once=1 root=2 bits=64 lane=32:16 stride=0 dynamic=0 clipped=0
// CHECK-NEXT: pair 0,1 exclusive=0
// CHECK-LABEL: function event_wait
// CHECK-NEXT: write 0 once=0 root=0 bits=64
// CHECK-LABEL: function join_view
// CHECK-NEXT: write 0 once=1 root=0 bits=64 fixed=0 direct=0
// CHECK-LABEL: function region_handle
// CHECK-NEXT: write 0 once=1 root=0 bits=64 lane=8:8 stride=0 dynamic=0 clipped=0 fixed=1 direct=0
// CHECK-LABEL: function loop_handle
// CHECK-NEXT: write 0 once=0 root=0 bits=64 lane=0:64 stride=0 dynamic=0 clipped=0 fixed=1 direct=0
// CHECK-LABEL: function capture_handle
// CHECK-NEXT: write 0 once=1 root=0 bits=64 lane=0:64 stride=0 dynamic=0 clipped=0 fixed=0 direct=0
// CHECK-LABEL: function join_nested_view
// The subview can select [2,6) or [58,62), so both lie in the effect hull [2,62).
// CHECK-NEXT: write 0 once=1 root=0 bits=64 fixed=0 direct=0 effect=2:60 dynamic=0
