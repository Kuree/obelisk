// RUN: obelisk-opt %s | FileCheck %s
//
// A representative cross-section of the semantic boundary using the canonical
// declarative assembly format.
module {
  %zero = obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
  %xz = obelisk.logic.constant -86 : i8, -1 : i8 : !obelisk.logic<8>
  %sum = obelisk.logic.binary add %zero, %xz
      : (!obelisk.logic<8>, !obelisk.logic<8>) -> !obelisk.logic<8>
  %known = obelisk.logic.is_known %sum : !obelisk.logic<8> -> i1
  %bits = obelisk.logic.to_bits %sum : !obelisk.logic<8> -> i8

  %var = obelisk.var.alloc = %sum : !obelisk.logic<8>
      : !obelisk.ref<!obelisk.logic<8>>
  obelisk.store %xz to %var
      : !obelisk.logic<8>, !obelisk.ref<!obelisk.logic<8>>
  %loaded = obelisk.load %var
      : !obelisk.ref<!obelisk.logic<8>> -> !obelisk.logic<8>

  %net = obelisk.net.alloc wire : !obelisk.net<!obelisk.logic<8>>
  obelisk.net.drive %net[0] = %loaded
      : (!obelisk.net<!obelisk.logic<8>>, !obelisk.logic<8>) -> ()

  %seed = arith.constant 42 : i64
  %rng = obelisk.random.stream_create %seed
  %random = obelisk.random.next %rng : i64

  %size = arith.constant 4 : i64
  %array = obelisk.dynarray.create %size
      : (i64) -> !obelisk.dynarray<!obelisk.logic<8>>
  %element = obelisk.dynarray.element_ref %array[%size]
      : (!obelisk.dynarray<!obelisk.logic<8>>, i64)
        -> !obelisk.ref<!obelisk.logic<8>>

  %event = obelisk.event.create
  obelisk.event.trigger %event in active nonblocking = false

  %time = obelisk.time.constant 10 : i64
  obelisk.trace "top.value" = %loaded at %time : !obelisk.logic<8>
}

// CHECK-LABEL: module {
// CHECK: obelisk.logic.constant 0 : i8, 0 : i8 : !obelisk.logic<8>
// CHECK: obelisk.logic.binary add
// CHECK: obelisk.var.alloc
// CHECK: obelisk.net.alloc wire
// CHECK: obelisk.net.drive
// CHECK: obelisk.random.stream_create
// CHECK: obelisk.dynarray.element_ref
// CHECK: obelisk.event.trigger
// CHECK: obelisk.trace "top.value"
