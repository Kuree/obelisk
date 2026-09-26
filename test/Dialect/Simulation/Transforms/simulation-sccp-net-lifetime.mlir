// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-sccp))' | FileCheck %s
// IEEE 1800-2023 4.9.1: a constant drive is not a lifetime constant until
// its first update executes. Partial or delayed writers also invalidate it.
module {
  obelisk_sim.design @late_drive {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "writer"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    // CHECK-LABEL: obelisk_sim.design @late_drive
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<8>>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<8>>
      %out = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %reader = obelisk_sim.spawn @reader(%ctx, %net, %out) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      %writer = obelisk_sim.spawn @writer(%ctx, %driver) : !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @reader
    // CHECK: %[[READ:.*]] = obelisk_sim.net.read
    // CHECK: obelisk_sim.ref.store %[[READ]]
    obelisk_sim.func private @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %out: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
    obelisk_sim.func private @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = obelisk_sim.time.constant 10
      obelisk_sim.suspend.delay %delay to ^resume
    ^resume:
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %driver = %one : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
  obelisk_sim.design @root_read {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "writer"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    // CHECK-LABEL: obelisk_sim.design @root_read
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<8>>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<8>>
      %out = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      // CHECK: %[[EARLY:.*]] = obelisk_sim.net.read
      // CHECK: obelisk_sim.ref.store %[[EARLY]]
      %early = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %early to %out : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.driver.drive %driver = %one : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      %reader = obelisk_sim.spawn @reader(%ctx, %net, %out) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @reader
    // CHECK: %[[ONE:.*]] = obelisk_sim.logic.constant 1 : i8, 0 : i8
    // CHECK: obelisk_sim.ref.store %[[ONE]]
    obelisk_sim.func private @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %out: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
  }
  obelisk_sim.design @partial_writer {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "writer"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    // CHECK-LABEL: obelisk_sim.design @partial_writer
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<8>>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<8>>
      %out = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %driver = %one : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      %reader = obelisk_sim.spawn @reader(%ctx, %net, %out) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      %writer = obelisk_sim.spawn @writer(%ctx, %driver) : !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @reader
    // CHECK: %[[READ:.*]] = obelisk_sim.net.read
    // CHECK: obelisk_sim.ref.store %[[READ]]
    obelisk_sim.func private @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %out: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
    obelisk_sim.func private @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %bit = obelisk_sim.driver.extract %driver from 0 : !obelisk_sim.driver<!obelisk_sim.logic<8>> -> !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
  obelisk_sim.design @delayed_writer {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "reader"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "writer"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    // CHECK-LABEL: obelisk_sim.design @delayed_writer
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<8>>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<8>>
      %out = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %driver = %one : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      %reader = obelisk_sim.spawn @reader(%ctx, %net, %out) : !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      %writer = obelisk_sim.spawn @writer(%ctx, %driver) : !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @reader
    // CHECK: %[[READ:.*]] = obelisk_sim.net.read
    // CHECK: obelisk_sim.ref.store %[[READ]]
    obelisk_sim.func private @reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %out: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.ref.store %value to %out : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return
    }
    obelisk_sim.func private @writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %delay = obelisk_sim.time.constant 10
      %zero = obelisk_sim.logic.constant 0 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.driver.drive_inertial %driver = %zero after [%delay, %delay, %delay] site 3 : 0 vector = false : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      obelisk_sim.return
    }
  }
}
