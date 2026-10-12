// RUN: obelisk-opt %s --canonicalize | FileCheck %s
// LRM 6.14, 6.17, 9.7, 15.3, 25.9: compare object identity, including null.
// These folds do not infer object status or contents from its handle.

// CHECK-LABEL: func.func @chandle_self(
// CHECK: arith.constant true
// CHECK: return
func.func @chandle_self(%a: !simulation.chandle, %b: !simulation.chandle) -> i1 {
  %r = simulation.chandle.equal %a, %a : !simulation.chandle
  return %r : i1
}

// CHECK-LABEL: func.func @chandle_different(
// CHECK: simulation.chandle.equal
// CHECK: return
func.func @chandle_different(%a: !simulation.chandle, %b: !simulation.chandle) -> i1 {
  %r = simulation.chandle.equal %a, %b : !simulation.chandle
  return %r : i1
}

// CHECK-LABEL: func.func @process_self(
// CHECK: arith.constant true
// CHECK: return
func.func @process_self(%a: !simulation.process, %b: !simulation.process) -> i1 {
  %r = simulation.process.equal %a, %a
  return %r : i1
}

// CHECK-LABEL: func.func @process_different(
// CHECK: simulation.process.equal
// CHECK: return
func.func @process_different(%a: !simulation.process, %b: !simulation.process) -> i1 {
  %r = simulation.process.equal %a, %b
  return %r : i1
}

// CHECK-LABEL: func.func @event_self(
// CHECK: arith.constant true
// CHECK: return
func.func @event_self(%a: !simulation.event, %b: !simulation.event) -> i1 {
  %r = simulation.event.equal %a, %a
  return %r : i1
}

// CHECK-LABEL: func.func @event_different(
// CHECK: simulation.event.equal
// CHECK: return
func.func @event_different(%a: !simulation.event, %b: !simulation.event) -> i1 {
  %r = simulation.event.equal %a, %b
  return %r : i1
}

// CHECK-LABEL: func.func @virtual_interface_self(
// CHECK: arith.constant true
// CHECK: return
func.func @virtual_interface_self(%a: !simulation.virtual_interface<"bus", "">, %b: !simulation.virtual_interface<"bus", "">) -> i1 {
  %r = simulation.virtual_interface.equal %a, %a : !simulation.virtual_interface<"bus", "">, !simulation.virtual_interface<"bus", "">
  return %r : i1
}

// CHECK-LABEL: func.func @virtual_interface_different(
// CHECK: simulation.virtual_interface.equal
// CHECK: return
func.func @virtual_interface_different(%a: !simulation.virtual_interface<"bus", "">, %b: !simulation.virtual_interface<"bus", "">) -> i1 {
  %r = simulation.virtual_interface.equal %a, %b : !simulation.virtual_interface<"bus", "">, !simulation.virtual_interface<"bus", "">
  return %r : i1
}

// CHECK-LABEL: func.func @same_modport
// CHECK-NEXT: return %arg0
func.func @same_modport(%a: !simulation.virtual_interface<"bus", "m">) -> !simulation.virtual_interface<"bus", "m"> {
  %r = simulation.virtual_interface.cast %a : !simulation.virtual_interface<"bus", "m"> to !simulation.virtual_interface<"bus", "m">
  return %r : !simulation.virtual_interface<"bus", "m">
}
// CHECK-LABEL: func.func @change_modport
// CHECK: simulation.virtual_interface.cast
// CHECK: return
func.func @change_modport(%a: !simulation.virtual_interface<"bus", "">) -> !simulation.virtual_interface<"bus", "m"> {
  %r = simulation.virtual_interface.cast %a : !simulation.virtual_interface<"bus", ""> to !simulation.virtual_interface<"bus", "m">
  return %r : !simulation.virtual_interface<"bus", "m">
}
// CHECK-LABEL: func.func @managed_null
// CHECK: arith.constant true
// CHECK-NOT: simulation.managed.is_null
// CHECK: return
func.func @managed_null() -> i1 {
  %n = simulation.managed.null : !simulation.dynamic_array<i8>
  %r = simulation.managed.is_null %n : (!simulation.dynamic_array<i8>) -> i1
  return %r : i1
}

// CHECK-LABEL: func.func @semaphore_self(
// CHECK: arith.constant true
// CHECK-NOT: simulation.semaphore.equal
// CHECK: return
func.func @semaphore_self(%a: !simulation.semaphore) -> i1 {
  %r = simulation.semaphore.equal %a, %a
  return %r : i1
}

// CHECK-LABEL: func.func @semaphore_different(
// CHECK: simulation.semaphore.equal
// CHECK: return
func.func @semaphore_different(%a: !simulation.semaphore, %b: !simulation.semaphore) -> i1 {
  %r = simulation.semaphore.equal %a, %b
  return %r : i1
}

// CHECK-LABEL: func.func @semaphore_nulls(
// CHECK: arith.constant true
// CHECK-NOT: simulation.semaphore.equal
// CHECK: return
func.func @semaphore_nulls() -> i1 {
  %a = simulation.managed.null : !simulation.semaphore
  %b = simulation.managed.null : !simulation.semaphore
  %r = simulation.semaphore.equal %a, %b
  return %r : i1
}
// CHECK-LABEL: func.func @mailbox_self(
// CHECK: arith.constant true
// CHECK-NOT: simulation.mailbox.equal
// CHECK: return
func.func @mailbox_self(%a: !simulation.mailbox<i32>) -> i1 {
  %r = simulation.mailbox.equal %a, %a : !simulation.mailbox<i32>
  return %r : i1
}

// CHECK-LABEL: func.func @mailbox_different(
// CHECK: simulation.mailbox.equal
// CHECK: return
func.func @mailbox_different(%a: !simulation.mailbox<i32>, %b: !simulation.mailbox<i32>) -> i1 {
  %r = simulation.mailbox.equal %a, %b : !simulation.mailbox<i32>
  return %r : i1
}

// CHECK-LABEL: func.func @mailbox_nulls(
// CHECK: arith.constant true
// CHECK-NOT: simulation.mailbox.equal
// CHECK: return
func.func @mailbox_nulls() -> i1 {
  %a = simulation.managed.null : !simulation.mailbox<i32>
  %b = simulation.managed.null : !simulation.mailbox<i32>
  %r = simulation.mailbox.equal %a, %b : !simulation.mailbox<i32>
  return %r : i1
}
