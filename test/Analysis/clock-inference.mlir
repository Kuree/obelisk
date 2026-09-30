// RUN: obelisk-opt %s --test-obelisk-clock-inference -o /dev/null 2>&1 | FileCheck %s
// The dense CFG join takes max, suspension resets the activation, and
// zero-time loops converge to many writes. Physical aliases share a domain.
module {
  simulation.design @clocks {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.storage.decl 4 in 0 : i1 design
    simulation.storage.decl 5 in 0 : i1 design
    simulation.storage.decl 6 in 0 : i1 design
    simulation.storage.decl 7 in 0 : i1 design
    simulation.storage.decl 8 in 0 : i1 design
    simulation.storage.decl 9 in 0 : i1 design
    simulation.storage.decl 10 in 0 : i1 design
    simulation.storage.decl 11 in 0 : i1 design
    simulation.storage.decl 12 in 0 : i1 design
    simulation.storage.decl 13 in 0 : i1 design
    simulation.storage.decl 14 in 0 : i1 design
    simulation.storage.decl 15 in 0 : i1 design
    simulation.storage.decl 16 in 0 : i1 design
    simulation.storage.decl 17 in 0 : i1 design
    simulation.storage.decl 18 in 0 : i1 design
    simulation.storage.decl 19 in 0 : i1 design
    simulation.storage.decl 20 in 0 : i1 design
    simulation.storage.decl 21 in 0 : i2 design
    simulation.storage.decl 22 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always hierarchy "source0"
    simulation.code_unit.decl 2 in 0 always hierarchy "source1"
    simulation.code_unit.decl 3 in 0 always hierarchy "source10"
    simulation.code_unit.decl 4 in 0 always hierarchy "copy2"
    simulation.code_unit.decl 5 in 0 always hierarchy "copy3"
    simulation.code_unit.decl 6 in 0 always hierarchy "gated"
    simulation.code_unit.decl 7 in 0 always hierarchy "double"
    simulation.code_unit.decl 8 in 0 always hierarchy "loop"
    simulation.code_unit.decl 9 in 0 always hierarchy "conflict0"
    simulation.code_unit.decl 10 in 0 always hierarchy "conflict1"
    simulation.code_unit.decl 11 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 12 in 0 always hierarchy "copy_constant"
    simulation.code_unit.decl 13 in 0 always hierarchy "cycle11"
    simulation.code_unit.decl 14 in 0 always hierarchy "cycle12"
    simulation.code_unit.decl 50 in 0 always hierarchy "divider"
    simulation.code_unit.decl 51 in 0 always hierarchy "divided_copy"
    simulation.code_unit.decl 52 in 0 always hierarchy "async_gate"
    simulation.code_unit.decl 53 in 0 always hierarchy "empty_loop"
    simulation.code_unit.decl 54 in 0 always hierarchy "delayed"
    simulation.code_unit.decl 70 in 0 always hierarchy "source18"
    simulation.code_unit.decl 71 in 0 always hierarchy "source20"
    simulation.code_unit.decl 72 in 0 always hierarchy "reference_copy"
    simulation.code_unit.decl 73 in 0 function hierarchy "formal_setter"
    simulation.code_unit.decl 74 in 0 always hierarchy "call_writer"
    simulation.code_unit.decl 75 in 0 always hierarchy "copy19"
    simulation.code_unit.decl 76 in 0 always hierarchy "wide_alias"
    simulation.code_unit.decl 77 in 0 always hierarchy "source22"
    simulation.code_unit.decl 78 in 0 function hierarchy "Setter.set"
    simulation.code_unit.decl 79 in 0 root_initializer hierarchy "bootstrap_setter"
    simulation.code_unit.decl 80 in 0 always hierarchy "virtual_writer"
    simulation.class.decl @Setter id 1 {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.method @Setter_set of @Setter slot 0 signature_id 17
        implemented_by @virtual_setter :
      (!simulation.context, !simulation.class_handle<@Setter>) -> () {
        is_final = true, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.func @source0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 3 : i32, test.clock_storage = 0 : i64, test.clock_half_period = 5 : i64} {
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
    simulation.func @source1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 3 : i32, test.clock_storage = 1 : i64, test.clock_half_period = 7 : i64} {
      %clock = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      cf.br ^tick
    ^tick:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %clock : i1, !simulation.ref<i1>
      %delay = simulation.time.constant 7
      simulation.suspend.delay %delay to ^tick
    }
    simulation.func @source10(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 3 : i64, entry_kind = 3 : i32, test.clock_storage = 10 : i64, test.clock_half_period = 5 : i64} {
      %clock = simulation.context.storage %ctx[10] : !simulation.ref<i1>
      cf.br ^tick
    ^tick:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %clock : i1, !simulation.ref<i1>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^tick
    }
    simulation.func @copy2(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 4 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @copy3(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 5 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @gated(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 6 : i64, entry_kind = 3 : i32, schedule.periodic_control} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[4] : !simulation.ref<i1>
      %condition = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge both %tick to ^choose : !simulation.ref<i1>
    ^choose:
      %cond = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %cond, ^yes, ^no
    ^yes:
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      cf.br ^wait
    ^no:
      %zero = arith.constant 0 : i1
      simulation.ref.store %zero to %out : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @double(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 7 : i64, entry_kind = 3 : i32, schedule.periodic_control} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[5] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge both %tick to ^write : !simulation.ref<i1>
    ^write:
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 8 : i64, entry_kind = 3 : i32, schedule.periodic_control} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[6] : !simulation.ref<i1>
      %condition = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge both %tick to ^write : !simulation.ref<i1>
    ^write:
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      %again = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %again, ^write, ^wait
    }
    simulation.func @conflict0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 9 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[7] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @conflict1(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 10 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[7] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 11 : i64, entry_kind = 0 : i32} {
      %constant = simulation.context.storage %ctx[8] : !simulation.ref<i1>
      %zero = arith.constant 0 : i1
      simulation.ref.store %zero to %constant : i1, !simulation.ref<i1>
      simulation.return
    }
    simulation.func @copy_constant(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 12 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[8] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[9] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @cycle11(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 13 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[11] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @cycle12(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 14 : i64, entry_kind = 3 : i32} {
      %in = simulation.context.storage %ctx[11] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @divider(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 50 : i64} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[13] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %tick to ^write : !simulation.ref<i1>
    ^write:
      %v = simulation.ref.load %out : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %out : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @divided_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 51 : i64} {
      %in = simulation.context.storage %ctx[13] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[14] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @async_gate(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 52 : i64} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[15] : !simulation.ref<i1>
      %enable = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      cf.br ^write
    ^write:
      %clock = simulation.ref.load %tick : !simulation.ref<i1> -> i1
      %en = simulation.ref.load %enable : !simulation.ref<i1> -> i1
      %gate = arith.andi %clock, %en : i1
      simulation.ref.store %gate to %out : i1, !simulation.ref<i1>
      simulation.suspend.any %tick, %enable edges [0, 0] to ^write : !simulation.ref<i1>, !simulation.ref<i1>
    }
    simulation.func @empty_loop(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 53 : i64} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[16] : !simulation.ref<i1>
      %condition = simulation.context.storage %ctx[12] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge both %tick to ^loop : !simulation.ref<i1>
    ^loop:
      %again = simulation.ref.load %condition : !simulation.ref<i1> -> i1
      cf.cond_br %again, ^loop, ^write
    ^write:
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @delayed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 54 : i64} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[17] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge both %tick to ^write : !simulation.ref<i1>
    ^write:
      %one = arith.constant 1 : i1
      %delay = simulation.time.constant 3
      simulation.nba.enqueue %one to %out after %delay : (i1, !simulation.ref<i1>, !simulation.time) -> ()
      cf.br ^wait
    }
    simulation.func @source18(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 70 : i64, test.clock_storage = 18 : i64, test.clock_half_period = 5 : i64} {
      %clock = simulation.context.storage %ctx[18] : !simulation.ref<i1>
      cf.br ^tick
    ^tick:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %clock : i1, !simulation.ref<i1>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^tick
    }
    simulation.func @source20(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 71 : i64, test.clock_storage = 20 : i64, test.clock_half_period = 5 : i64} {
      %clock = simulation.context.storage %ctx[20] : !simulation.ref<i1>
      cf.br ^tick
    ^tick:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %clock : i1, !simulation.ref<i1>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^tick
    }
    simulation.func @reference_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 72 : i64} {
      %in = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[18] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      simulation.ref.copy %in to %out : !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    simulation.func @formal_setter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %target: !simulation.ref<i1> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 73 : i64} {
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %target : i1, !simulation.ref<i1>
      simulation.return
    }
    simulation.func @call_writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 74 : i64} {
      %tick = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[20] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %tick to ^write : !simulation.ref<i1>
    ^write:
      simulation.call @formal_setter(%ctx, %out) : (!simulation.context, !simulation.ref<i1>) -> ()
      cf.br ^wait
    }
    simulation.func @copy19(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 75 : i64} {
      %in = simulation.context.storage %ctx[18] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[19] : !simulation.ref<i1>
      cf.br ^copy
    ^copy:
      %v = simulation.ref.load %in : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %out : i1, !simulation.ref<i1>
      simulation.suspend.change %in to ^copy : !simulation.ref<i1>
    }
    // A whole-root store also writes the candidate bit through another view.
    // Counting just one-bit destinations would incorrectly certify cadence.
    simulation.func @wide_alias(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 76 : i64} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %root = simulation.context.storage %ctx[21] : !simulation.ref<i2>
      %out = simulation.ref.extract %root from 0 : !simulation.ref<i2> -> !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %tick to ^write : !simulation.ref<i1>
    ^write:
      %one = arith.constant 1 : i1
      %wide = arith.constant 3 : i2
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      simulation.ref.store %wide to %root : i2, !simulation.ref<i2>
      cf.br ^wait
    }
    // A method reached by bootstrap and later virtual dispatch is a runtime
    // writer, even when its global destination is not a reference argument.
    simulation.func @source22(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 77 : i64, test.clock_storage = 22 : i64, test.clock_half_period = 5 : i64} {
      %clock = simulation.context.storage %ctx[22] : !simulation.ref<i1>
      cf.br ^tick
    ^tick:
      %v = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i1
      %toggle = arith.xori %v, %one : i1
      simulation.ref.store %toggle to %clock : i1, !simulation.ref<i1>
      %delay = simulation.time.constant 5
      simulation.suspend.delay %delay to ^tick
    }
    simulation.func private @virtual_setter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Setter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 78 : i64} {
      %out = simulation.context.storage %ctx[22] : !simulation.ref<i1>
      %one = arith.constant 1 : i1
      simulation.ref.store %one to %out : i1, !simulation.ref<i1>
      simulation.return
    }
    simulation.func @bootstrap_setter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 79 : i64} {
      %this = simulation.class.alloc %ctx : !simulation.context -> !simulation.class_handle<@Setter>
      simulation.class.direct_call @virtual_setter %this() : (!simulation.class_handle<@Setter>) -> ()
      simulation.return
    }
    simulation.func @virtual_writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %this: !simulation.class_handle<@Setter> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 80 : i64} {
      %tick = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %tick to ^write : !simulation.ref<i1>
    ^write:
      simulation.class.virtual_call %this[@Setter_set] slot 0 signature_id 17() : (!simulation.class_handle<@Setter>) -> ()
      cf.br ^wait
    }
  }
}
// CHECK: clock 0: periodic domain=1:0 half-period=5
// CHECK-NEXT: clock 1: periodic domain=2:8 half-period=7
// CHECK-NEXT: clock 2: periodic domain=1:0 half-period=5
// CHECK-NEXT: clock 3: periodic domain=1:0 half-period=5
// CHECK-NEXT: clock 4: tick-driven domain=1:0 half-period=5
// CHECK-NEXT: clock 5: unknown
// CHECK-NEXT: clock 6: unknown
// CHECK-NEXT: clock 7: unknown
// CHECK-NEXT: clock 8: constant=0
// CHECK-NEXT: clock 9: constant=0
// CHECK-NEXT: clock 10: periodic domain=11:80 half-period=5
// CHECK-NEXT: clock 11: bottom
// CHECK-NEXT: clock 12: bottom
// CHECK-NEXT: clock 13: tick-driven domain=1:0 half-period=5
// CHECK-NEXT: clock 14: tick-driven domain=1:0 half-period=5
// CHECK-NEXT: clock 15: unknown
// CHECK-NEXT: clock 16: tick-driven domain=1:0 half-period=5
// CHECK-NEXT: clock 17: unknown
// CHECK-NEXT: clock 18: unknown
// CHECK-NEXT: clock 19: unknown
// CHECK-NEXT: clock 20: unknown
// CHECK-NEXT: clock 21: unknown
// CHECK-NEXT: clock 22: unknown
