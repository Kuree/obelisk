// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.middle set={propagation_delays = array<i64: 1, 1, 1>}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.driver set={strength0 = 3 : i32, strength1 = 3 : i32}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=full},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=FULL
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.middle set={resolution_kind = 5 : i32}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUNDARY
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-edit-attributes{has=test.shared_writer set={id = 0 : i64}},simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o /dev/null 2>&1 | FileCheck %s --check-prefix=CONFLICT

// LRM 4.3, 4.9.1, 6.6.1 and 9.4.2: every copy in the clock tree must
// update before its edge consumers execute. Initialization remains scheduled;
// all driver planes must be reconstructed at the timed runtime handoff.
// A delayed, resolved or multiply driven intermediate remains an execution
// boundary. A second process writing the same driver contribution must also
// break reachability, even with one driver declaration. Full VPI cannot turn
// an externally writable copy into a projection.
// TRACE: obelisk periodic forwarding: aliases=4
// BOUNDARY: obelisk periodic forwarding: aliases=2
// FULL: obelisk periodic forwarding: aliases=0
// CONFLICT: obelisk periodic forwarding: aliases=1
// CHECK-LABEL: llvm.mlir.global internal constant @__obelisk_periodic_alias_plan_v1
// CHECK-SAME: {{\[\[}}[[ROOT:[0-9]+]] : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, [[SOURCE:[0-9]+]], {{[0-9]+}}, {{[0-9]+}}
// CHECK-SAME: {{\], \[}}[[ROOT]] : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, [[SOURCE]], {{[0-9]+}}, {{[0-9]+}}
// CHECK-SAME: {{\], \[}}[[ROOT]] : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, [[SOURCE]], {{[0-9]+}}, {{[0-9]+}}
// CHECK-SAME: {{\], \[}}[[ROOT]] : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, {{[0-9]+}} : i32, [[SOURCE]], {{[0-9]+}}, {{[0-9]+}}
// CHECK-SAME: !llvm.array<4 x

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
!net = !simulation.net<!bit>
!driver = !simulation.driver<!bit>
!count = !simulation.ref<i32>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32, native.debug_timing, native.closed_executable} {
  simulation.design @periodic_tree {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.net.decl 0 in 0 : !bit design
    simulation.net.decl 1 in 0 : !bit design {test.middle}
    simulation.net.decl 2 in 0 : !bit design
    // A disconnected copy cycle must not acquire a periodic root.
    simulation.net.decl 3 in 0 : !bit design
    simulation.net.decl 4 in 0 : !bit design
    simulation.driver.decl 3 in 0 drives 3 : !bit design
    simulation.driver.decl 4 in 0 drives 4 : !bit design
    simulation.driver.decl 0 in 0 drives 0 : !bit design
    simulation.driver.decl 1 in 0 drives 1 : !bit design {test.driver}
    simulation.driver.decl 2 in 0 drives 2 : !bit design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "first"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "middle"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "last"
    simulation.code_unit.decl 6 in 0 always hierarchy "counter"
    simulation.code_unit.decl 7 in 0 initial hierarchy "test"
    simulation.code_unit.decl 8 in 0 continuous hierarchy "cycle_a"
    simulation.code_unit.decl 9 in 0 continuous hierarchy "cycle_b"
    simulation.code_unit.decl 11 in 0 continuous hierarchy "bridge"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %count = simulation.context.storage %ctx[1] : !count
      %zero = arith.constant false
      %zero32 = arith.constant 0 : i32
      simulation.ref.store %zero to %clock : i1, !simulation.ref<i1>
      simulation.ref.store %zero32 to %count : i32, !count
      %c = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      // Reverse source order exercises propagation through already waiting
      // copies during time-zero initialization (4.9.1).
      %d = simulation.spawn @last(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @middle(%ctx) : !simulation.context -> !simulation.process
      %h = simulation.spawn @bridge(%ctx) : !simulation.context -> !simulation.process
      %a = simulation.spawn @first(%ctx) : !simulation.context -> !simulation.process
      %f = simulation.spawn @cycle_a(%ctx) : !simulation.context -> !simulation.process
      %g = simulation.spawn @cycle_b(%ctx) : !simulation.context -> !simulation.process
      %e = simulation.spawn @counter(%ctx) : !simulation.context -> !simulation.process
      %t = simulation.spawn @test(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %clock = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^tick
    ^tick:
      %old = simulation.ref.load %clock : !simulation.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      simulation.ref.store %next to %clock : i1, !simulation.ref<i1>
      cf.br ^wait
    }
    simulation.func @bridge(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 11 : i64} {
      %source = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %target = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %source : !simulation.ref<i1> -> i1
      simulation.ref.store %value to %target {simulation.continuous_store} : i1, !simulation.ref<i1>
      simulation.suspend.change %source to ^body : !simulation.ref<i1>
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      %source = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %driver = simulation.context.driver %ctx[0] : !driver
      cf.br ^body
    ^body:
      %bits = simulation.ref.load %source : !simulation.ref<i1> -> i1
      %value = simulation.logic.from_bits %bits : i1 -> !bit
      simulation.driver.drive %driver = %value : !driver, !bit
      simulation.suspend.change %source to ^body : !simulation.ref<i1>
    }
    simulation.func @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %source = simulation.context.net %ctx[0] : !net
      %driver = simulation.context.driver %ctx[1] : !driver
      cf.br ^body
    ^body:
      %value = simulation.net.read %source : !net -> !bit
      simulation.driver.drive %driver = %value : !driver, !bit
      simulation.suspend.change %source to ^body : !net
    }
    simulation.func @last(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      %source = simulation.context.net %ctx[1] : !net
      %driver = simulation.context.driver %ctx[2] : !driver
      cf.br ^body
    ^body:
      %value = simulation.net.read %source : !net -> !bit
      simulation.driver.drive %driver = %value : !driver, !bit
      simulation.suspend.change %source to ^body : !net
    }
    simulation.func @cycle_a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 8 : i64} {
      %source = simulation.context.net %ctx[4] : !net
      %driver = simulation.context.driver %ctx[3] {test.shared_writer} : !driver
      cf.br ^body
    ^body:
      %value = simulation.net.read %source : !net -> !bit
      simulation.driver.drive %driver = %value : !driver, !bit
      simulation.suspend.change %source to ^body : !net
    }
    simulation.func @cycle_b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 9 : i64} {
      %source = simulation.context.net %ctx[3] : !net
      %driver = simulation.context.driver %ctx[4] : !driver
      cf.br ^body
    ^body:
      %value = simulation.net.read %source : !net -> !bit
      simulation.driver.drive %driver = %value : !driver, !bit
      simulation.suspend.change %source to ^body : !net
    }
    simulation.func @counter(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 6 : i64} {
      %clock = simulation.context.net %ctx[2] : !net
      %count = simulation.context.storage %ctx[1] : !count
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^tick : !net
    ^tick:
      %old = simulation.ref.load %count : !count -> i32
      %one = arith.constant 1 : i32
      %next = arith.addi %old, %one : i32
      simulation.nba.enqueue %next to %count : (i32, !count) -> ()
      cf.br ^wait
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      %delay = simulation.time.constant 7
      simulation.suspend.delay %delay to ^first_report
    ^first_report:
      %count0 = simulation.context.storage %ctx[1] : !count
      %value0 = simulation.ref.load %count0 : !count -> i32
      %net0 = simulation.context.net %ctx[2] : !net
      %level0 = simulation.net.read %net0 : !net -> !bit
      %driver0 = simulation.context.driver %ctx[2] : !driver
      %raw0 = simulation.driver.read %driver0 : !driver -> !bit
      %fmt0 = simulation.bytes.constant "count=%0d level=%b driver=%b"
      %stdout0 = arith.constant -2147483647 : i32
      simulation.display %ctx to %stdout0(%fmt0, %value0, %level0, %raw0) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i32, !bit, !bit
      %remaining = simulation.time.constant 6
      simulation.suspend.delay %remaining to ^report
    ^report:
      %count = simulation.context.storage %ctx[1] : !count
      %value = simulation.ref.load %count : !count -> i32
      %net = simulation.context.net %ctx[2] : !net
      %level = simulation.net.read %net : !net -> !bit
      %driver = simulation.context.driver %ctx[2] : !driver
      %raw = simulation.driver.read %driver : !driver -> !bit
      %fmt = simulation.bytes.constant "count=%0d level=%b driver=%b"
      %stdout = arith.constant -2147483647 : i32
      simulation.display %ctx to %stdout(%fmt, %value, %level, %raw) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, i32, !bit, !bit
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
