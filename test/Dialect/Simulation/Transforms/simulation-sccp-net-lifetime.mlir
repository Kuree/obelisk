// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s < %t.threaded
// IEEE 1800-2023 4.9.1: a constant drive is not a lifetime constant until
// its first update executes. Partial or delayed writers also invalidate it.
module {
  simulation.design @late_drive {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 3 in 0 initial hierarchy "writer"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // CHECK-LABEL: simulation.design @late_drive
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %reader = simulation.spawn @reader(%ctx, %net, %out) : !simulation.context, !simulation.net<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %writer = simulation.spawn @writer(%ctx, %driver) : !simulation.context, !simulation.driver<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @reader
    // CHECK: %[[READ:.*]] = simulation.net.read
    // CHECK: simulation.ref.store %[[READ]]
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %driver: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = simulation.time.constant 10
      simulation.suspend.delay %delay to ^resume
    ^resume:
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      simulation.driver.drive %driver = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.return
    }
  }
  simulation.design @root_read {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 3 in 0 initial hierarchy "writer"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // CHECK-LABEL: simulation.design @root_read
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      // CHECK: %[[EARLY:.*]] = simulation.net.read
      // CHECK: simulation.ref.store %[[EARLY]]
      %early = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %early to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.driver.drive %driver = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %reader = simulation.spawn @reader(%ctx, %net, %out) : !simulation.context, !simulation.net<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @reader
    // CHECK: %[[ONE:.*]] = simulation.logic.constant 1 : i8, 0 : i8
    // CHECK: simulation.ref.store %[[ONE]]
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
  }
  simulation.design @partial_writer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 3 in 0 initial hierarchy "writer"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // CHECK-LABEL: simulation.design @partial_writer
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      simulation.driver.drive %driver = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %reader = simulation.spawn @reader(%ctx, %net, %out) : !simulation.context, !simulation.net<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %writer = simulation.spawn @writer(%ctx, %driver) : !simulation.context, !simulation.driver<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @reader
    // CHECK: %[[READ:.*]] = simulation.net.read
    // CHECK: simulation.ref.store %[[READ]]
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %driver: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %bit = simulation.driver.extract %driver from 0 : !simulation.driver<!simulation.logic<8>> -> !simulation.driver<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %bit = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }
  }
  simulation.design @delayed_writer {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "reader"
    simulation.code_unit.decl 3 in 0 initial hierarchy "writer"
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    // CHECK-LABEL: simulation.design @delayed_writer
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      simulation.driver.drive %driver = %one : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %reader = simulation.spawn @reader(%ctx, %net, %out) : !simulation.context, !simulation.net<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %writer = simulation.spawn @writer(%ctx, %driver) : !simulation.context, !simulation.driver<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @reader
    // CHECK: %[[READ:.*]] = simulation.net.read
    // CHECK: simulation.ref.store %[[READ]]
    simulation.func private @reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %out: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }
    simulation.func private @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %driver: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = simulation.time.constant 10
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.driver.drive_inertial %driver = %zero after [%delay, %delay, %delay] site 3 : 0 vector = false : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      simulation.return
    }
  }
}
