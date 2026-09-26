// Native coroutine globals and canonical bytecode state need not share an
// authoritative buffer. Delayed completion followed by an unqualified write
// must read the live destination, publish its change, and cancel a pending
// same-target path write (LRM 30.4, 30.5). Cover native-only and bytecode images.
// CHECK: delayed 1 1
// CHECK-NEXT: immediate 0 1
// CHECK-NEXT: observed 0 2
// CHECK-NEXT: cancelled 0 2
!logic = !obelisk_sim.logic<1>
!ref = !obelisk_sim.ref<!logic>
!count = !obelisk_sim.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @procedural_path_native {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !logic design
    obelisk_sim.storage.decl 1 in 0 : i32 design
    obelisk_sim.storage.decl 2 in 0 : !logic design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "test"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "observer"
    obelisk_sim.code_unit.decl 4 in 0 observer hierarchy "monitor"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %data = obelisk_sim.context.storage %ctx[0] : !ref
      %count = obelisk_sim.context.storage %ctx[1] : !count
      %scratch = obelisk_sim.context.storage %ctx[2] : !ref
      %zero = obelisk_sim.logic.constant false, false : !logic
      %none = arith.constant 0 : i32
      obelisk_sim.ref.store %zero to %data : !logic, !ref
      obelisk_sim.ref.store %zero to %scratch : !logic, !ref
      obelisk_sim.ref.store %none to %count : i32, !count
      %observer = obelisk_sim.spawn @observer(%ctx, %data, %count) : !obelisk_sim.context, !ref, !count -> !obelisk_sim.process
      %test = obelisk_sim.spawn @test(%ctx, %data, %count) : !obelisk_sim.context, !ref, !count -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @observer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %count: !count {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      %scratch = obelisk_sim.context.storage %ctx[2] : !ref
      %initial = obelisk_sim.ref.load %data : !ref -> !logic
      %watch = obelisk_sim.observer.bind @monitor values(%data, %scratch, %data : !ref, !ref, !ref) captures 2 : <!logic>
      obelisk_sim.suspend.observe %watch, %initial conditions 0 edges [0] indices [-1] to ^change : !obelisk_sim.observer<!logic>, !logic
    ^change:
      %old = obelisk_sim.ref.load %count : !count -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      obelisk_sim.ref.store %next to %count : i32, !count
      cf.br ^wait
    }
    // Derived event-primary monitors keep per-source snapshots. Their writes
    // must be visible to subsequent canonical reads in the same callback.
    obelisk_sim.func private @monitor(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %scratch: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) -> !logic attributes {entry_kind = 14 : i32, code_unit_id = 4 : i64, obelisk_sim.observer_result = 1 : i32, obelisk_sim.observer_width = 1 : i32, obelisk_sim.observer_four_state = true} {
      %value = obelisk_sim.ref.load %data : !ref -> !logic
      obelisk_sim.ref.store %value to %scratch : !logic, !ref
      %snapshot = obelisk_sim.ref.load %scratch : !ref -> !logic
      obelisk_sim.return %snapshot : !logic
    }
    obelisk_sim.func @test(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %count: !count {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %zero = obelisk_sim.logic.constant false, false : !logic
      %one = obelisk_sim.logic.constant true, false : !logic
      %yes = arith.constant true
      %no = arith.constant false
      %delay = obelisk_sim.time.constant 2
      %three = obelisk_sim.time.constant 3
      %delta = obelisk_sim.time.constant 0
      %stdout = arith.constant 1 : i32
      %fmt0 = obelisk_sim.bytes.constant "delayed %b %0d"
      %fmt1 = obelisk_sim.bytes.constant "immediate %b %0d"
      %fmt2 = obelisk_sim.bytes.constant "observed %b %0d"
      %fmt3 = obelisk_sim.bytes.constant "cancelled %b %0d"
      obelisk_sim.ref.store_inertial_path %one to %data write %yes active %yes masks[%yes, %yes, %yes] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      obelisk_sim.suspend.delay %three to ^delayed
    ^delayed:
      %v0 = obelisk_sim.ref.load %data : !ref -> !logic
      %c0 = obelisk_sim.ref.load %count : !count -> i32
      obelisk_sim.display %ctx to %stdout(%fmt0, %v0, %c0) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !logic, i32
      obelisk_sim.ref.store_inertial_path %zero to %data write %yes active %no masks[%no, %no, %no] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      %v1 = obelisk_sim.ref.load %data : !ref -> !logic
      %c1 = obelisk_sim.ref.load %count : !count -> i32
      obelisk_sim.display %ctx to %stdout(%fmt1, %v1, %c1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !logic, i32
      obelisk_sim.suspend.delay %delta to ^observed
    ^observed:
      %v2 = obelisk_sim.ref.load %data : !ref -> !logic
      %c2 = obelisk_sim.ref.load %count : !count -> i32
      obelisk_sim.display %ctx to %stdout(%fmt2, %v2, %c2) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !logic, i32
      obelisk_sim.ref.store_inertial_path %one to %data write %yes active %yes masks[%yes, %yes, %yes] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      obelisk_sim.ref.store_inertial_path %zero to %data write %yes active %no masks[%no, %no, %no] after[%delay, %delay, %delay] site 2 : 0 group 0 of 1 nonblocking = false : !ref, !logic, i1
      obelisk_sim.suspend.delay %three to ^cancelled
    ^cancelled:
      %v3 = obelisk_sim.ref.load %data : !ref -> !logic
      %c3 = obelisk_sim.ref.load %count : !count -> i32
      obelisk_sim.display %ctx to %stdout(%fmt3, %v3, %c3) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !logic, i32
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
