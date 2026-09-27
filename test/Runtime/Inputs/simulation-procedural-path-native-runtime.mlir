// Native coroutine globals and canonical bytecode state need not share an
// authoritative buffer. Delayed completion followed by an unqualified write
// must read the live destination, publish its change, and cancel a pending
// same-target path write (LRM 30.4, 30.5). Cover native-only and bytecode images.
// CHECK: delayed 1 1
// CHECK-NEXT: immediate 0 1
// CHECK-NEXT: observed 0 2
// CHECK-NEXT: cancelled 0 2
!logic = !simulation.logic<1>
!ref = !simulation.ref<!logic>
!count = !simulation.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @procedural_path_native {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !logic design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : !logic design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "test"
    simulation.code_unit.decl 3 in 0 always hierarchy "observer"
    simulation.code_unit.decl 4 in 0 observer hierarchy "monitor"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %data = simulation.context.storage %ctx[0] : !ref
      %count = simulation.context.storage %ctx[1] : !count
      %scratch = simulation.context.storage %ctx[2] : !ref
      %zero = simulation.logic.constant false, false : !logic
      %none = arith.constant 0 : i32
      simulation.ref.store %zero to %data : !logic, !ref
      simulation.ref.store %zero to %scratch : !logic, !ref
      simulation.ref.store %none to %count : i32, !count
      %observer = simulation.spawn @observer(%ctx, %data, %count) : !simulation.context, !ref, !count -> !simulation.process
      %test = simulation.spawn @test(%ctx, %data, %count) : !simulation.context, !ref, !count -> !simulation.process
      simulation.return
    }
    simulation.func @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %count: !count {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %scratch = simulation.context.storage %ctx[2] : !ref
      %initial = simulation.ref.load %data : !ref -> !logic
      %watch = simulation.observer.bind @monitor values(%data, %scratch, %data : !ref, !ref, !ref) captures 2 : <!logic>
      simulation.suspend.observe %watch, %initial conditions 0 edges [0] indices [-1] to ^change : !simulation.observer<!logic>, !logic
    ^change:
      %old = simulation.ref.load %count : !count -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.ref.store %next to %count : i32, !count
      cf.br ^wait
    }
    // Derived event-primary monitors keep per-source snapshots. Their writes
    // must be visible to subsequent canonical reads in the same callback.
    simulation.func private @monitor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %scratch: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) -> !logic attributes {entry_kind = 14 : i32, code_unit_id = 4 : i64, simulation.observer_result = 1 : i32, schedule.observer_width = 1 : i32, schedule.observer_four_state = true} {
      %value = simulation.ref.load %data : !ref -> !logic
      simulation.ref.store %value to %scratch : !logic, !ref
      %snapshot = simulation.ref.load %scratch : !ref -> !logic
      simulation.return %snapshot : !logic
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %data: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %count: !count {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = simulation.logic.constant false, false : !logic
      %one = simulation.logic.constant true, false : !logic
      %yes = arith.constant true
      %no = arith.constant false
      %delay = simulation.time.constant 2
      %three = simulation.time.constant 3
      %delta = simulation.time.constant 0
      %stdout = arith.constant 1 : i32
      %fmt0 = simulation.bytes.constant "delayed %b %0d"
      %fmt1 = simulation.bytes.constant "immediate %b %0d"
      %fmt2 = simulation.bytes.constant "observed %b %0d"
      %fmt3 = simulation.bytes.constant "cancelled %b %0d"
      simulation.ref.store_inertial_path %one to %data write %yes active %yes masks[%yes, %yes, %yes] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      simulation.suspend.delay %three to ^delayed
    ^delayed:
      %v0 = simulation.ref.load %data : !ref -> !logic
      %c0 = simulation.ref.load %count : !count -> i32
      simulation.display %ctx to %stdout(%fmt0, %v0, %c0) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !logic, i32
      simulation.ref.store_inertial_path %zero to %data write %yes active %no masks[%no, %no, %no] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      %v1 = simulation.ref.load %data : !ref -> !logic
      %c1 = simulation.ref.load %count : !count -> i32
      simulation.display %ctx to %stdout(%fmt1, %v1, %c1) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !logic, i32
      simulation.suspend.delay %delta to ^observed
    ^observed:
      %v2 = simulation.ref.load %data : !ref -> !logic
      %c2 = simulation.ref.load %count : !count -> i32
      simulation.display %ctx to %stdout(%fmt2, %v2, %c2) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !logic, i32
      simulation.ref.store_inertial_path %one to %data write %yes active %yes masks[%yes, %yes, %yes] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      simulation.ref.store_inertial_path %zero to %data write %yes active %no masks[%no, %no, %no] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      simulation.suspend.delay %three to ^cancelled
    ^cancelled:
      %v3 = simulation.ref.load %data : !ref -> !logic
      %c3 = simulation.ref.load %count : !count -> i32
      simulation.display %ctx to %stdout(%fmt3, %v3, %c3) newline = true radix = <decimal> flags = [0, 0, 0] : !simulation.bytes, !logic, i32
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
