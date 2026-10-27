// RUN: talos-opt %s --test-talos-clock-inference -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 23.3.3, 10.3.1: a port connection or net declaration
// assignment is a continuous assignment from its source to the net, and it
// updates the net only when the value changes (10.3.2). A single driver on a
// wire passes every value through (6.6.1, Table 6-2), and `~` complements 0
// and 1 while mapping x and z to x (Table 11-15). Such copies follow their
// source exactly, complemented or not. A tri0 net replaces z with 0, so it is
// periodic but not an exact copy; a net with two drivers is not a copy.

// CHECK: clock 0: periodic domain=1:0 half-period=5
// CHECK-NEXT: net 0: periodic domain=1:0 half-period=5 origin=1:0{{$}}
// CHECK-NEXT: net 1: periodic domain=1:0 half-period=5 inverted origin=1:0 complement{{$}}
// CHECK-NEXT: net 2: periodic domain=1:0 half-period=5 origin=1:0{{$}}
// CHECK-NEXT: net 3: periodic domain=1:0 half-period=5{{$}}
// CHECK-NEXT: net 4: unknown{{$}}
!bit = !simulation.logic<1>
module {
  simulation.design @polarity {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.net.decl 0 in 0 : !bit design
    simulation.net.decl 1 in 0 : !bit design
    simulation.net.decl 2 in 0 : !bit design
    simulation.net.decl 3 in 0 : !bit design {resolution_kind = 5 : i32}
    simulation.net.decl 4 in 0 : !bit design
    simulation.driver.decl 0 in 0 drives 0 : !bit design
    simulation.driver.decl 1 in 0 drives 1 : !bit design
    simulation.driver.decl 2 in 0 drives 2 : !bit design
    simulation.driver.decl 3 in 0 drives 3 : !bit design
    simulation.driver.decl 4 in 0 drives 4 : !bit design
    simulation.driver.decl 5 in 0 drives 4 : !bit design
    simulation.code_unit.decl 1 in 0 always hierarchy "source"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "port"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "invert"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "invert_again"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "pulled"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "first"
    simulation.code_unit.decl 7 in 0 continuous hierarchy "second"
    simulation.func @source(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 3 : i32, test.clock_storage = 0 : i64, test.clock_half_period = 5 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      cf.br ^tick
    ^tick:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %clock : i1, !simulation.ref<i1>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^tick
    }
    // A 2-state variable connected to a 4-state input net.
    simulation.func @port(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 9 : i32} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %logic = simulation.logic.from_bits %v : i1 -> !bit
      simulation.driver.drive %driver = %logic : !simulation.driver<!bit>, !bit
      simulation.suspend.change %clock to ^copy : !simulation.ref<i1>
    }
    // wire inverted = ~net0;
    simulation.func @invert(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 3 : i64, entry_kind = 7 : i32} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[1] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      %n = simulation.logic.unary bit_not %v : (!bit) -> !bit
      simulation.driver.drive %driver = %n : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
    simulation.func @invert_again(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 4 : i64, entry_kind = 7 : i32} {
      %net = simulation.context.net %ctx[1] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[2] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      %n = simulation.logic.unary bit_not %v : (!bit) -> !bit
      simulation.driver.drive %driver = %n : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
    simulation.func @pulled(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 5 : i64, entry_kind = 7 : i32} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[3] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      simulation.driver.drive %driver = %v : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 6 : i64, entry_kind = 7 : i32} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[4] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      simulation.driver.drive %driver = %v : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 7 : i64, entry_kind = 7 : i32} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!bit>
      %driver = simulation.context.driver %ctx[5] : !simulation.driver<!bit>
      cf.br ^copy
    ^copy:
      %v = simulation.net.read %net : !simulation.net<!bit> -> !bit
      simulation.driver.drive %driver = %v : !simulation.driver<!bit>, !bit
      simulation.suspend.change %net to ^copy : !simulation.net<!bit>
    }
  }
}
