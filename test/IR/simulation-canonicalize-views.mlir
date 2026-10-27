// RUN: talos-opt %s --canonicalize | FileCheck %s
// RUN: talos-opt %s --canonicalize="top-down=false" | FileCheck %s

!mixed = !simulation.packed_union<fields = [#simulation.field<name = "four", type = !simulation.logic<16>, ordinal = 0, packedOffset = 0>, #simulation.field<name = "two", type = i16, ordinal = 1, packedOffset = 0>], isTagged = false>
!struct = !simulation.packed_struct<[
  #simulation.field<name = "low", type = i8, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "high", type = !simulation.logic<8>, ordinal = 1, packedOffset = 8>
]>
!array = !simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>

func.func private @escape(!simulation.ref<!simulation.logic<24>>)

// LRM 7.2.1, 7.3.1: a two-state member of a four-state packed union occupies
// four-state bits. When the local escapes and cannot be promoted, the member
// view must still be valid IR: view the four-state bits and convert at each
// access (a ref.extract cannot change the state domain).
// CHECK-LABEL: func.func @mixed_escape(
// CHECK: %[[LOCAL:.*]] = simulation.ref.alloc
// CHECK: %[[VIEW:.*]] = simulation.ref.extract %[[LOCAL]] from 4 : !simulation.ref<!simulation.logic<24>> -> !simulation.ref<!simulation.logic<16>>
// CHECK: %[[KNOWN:.*]] = simulation.logic.from_bits %arg1 : i16 -> !simulation.logic<16>
// CHECK: simulation.ref.store %[[KNOWN]] to %[[VIEW]]
// CHECK: %[[LOW:.*]] = simulation.ref.extract %[[LOCAL]] from 8 : !simulation.ref<!simulation.logic<24>> -> !simulation.ref<!simulation.logic<8>>
// CHECK: %[[BITS:.*]] = simulation.ref.load %[[LOW]] : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
// CHECK: simulation.logic.to_bits %[[BITS]] : !simulation.logic<8> -> i8
func.func @mixed_escape(%initial: !simulation.logic<24>, %part: i16) -> i16 {
  %local = simulation.ref.alloc %initial : !simulation.logic<24> -> !simulation.ref<!simulation.logic<24>>
  %union = simulation.ref.extract %local from 4 : !simulation.ref<!simulation.logic<24>> -> !simulation.ref<!mixed>
  %two = simulation.ref.subelement %union[[1]] : !simulation.ref<!mixed> -> !simulation.ref<i16>
  simulation.ref.store %part to %two : i16, !simulation.ref<i16>
  %low = simulation.ref.extract %two from 4 : !simulation.ref<i16> -> !simulation.ref<i8>
  func.call @escape(%local) : (!simulation.ref<!simulation.logic<24>>) -> ()
  %v = simulation.ref.load %low : !simulation.ref<i8> -> i8
  %w = arith.extui %v : i8 to i16
  return %w : i16
}

// The same holds for a member of any storage, not only a local cell: a store
// writes known bits and a load converts X and Z to zero. A packed-array member
// keeps its shape so that a dynamic element selection still applies, and a
// user that is not a packed access keeps the two-state view.
!bits = !simulation.packed_struct<[
  #simulation.field<name = "h", type = !simulation.packed_array<3 : 0 x i1>, ordinal = 0, packedOffset = 1>,
  #simulation.field<name = "l", type = !simulation.logic<1>, ordinal = 1, packedOffset = 0>
]>
func.func private @observe(!simulation.ref<i8>)
// CHECK-LABEL: func.func @storage_member(
// CHECK: %[[ONE:.*]] = simulation.logic.constant true, false : !simulation.logic<1>
// CHECK: %[[LOW:.*]] = simulation.ref.extract %arg0 from 0 : !simulation.ref<!simulation.packed_struct<{{.*}}>> -> !simulation.ref<!simulation.logic<8>>
// CHECK: %[[TWO:.*]] = simulation.ref.subelement %arg0{{\[\[}}0{{\]\]}}
// CHECK: %[[KNOWN:.*]] = simulation.logic.from_bits %arg2 : i8 -> !simulation.logic<8>
// CHECK: simulation.ref.store %[[KNOWN]] to %[[LOW]]
// CHECK: %[[H:.*]] = simulation.ref.extract %arg1 from 1 : {{.*}} -> !simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>>
// CHECK: %[[BIT:.*]] = simulation.ref.array_element %[[H]][%arg3] : (!simulation.ref<!simulation.packed_array<3 : 0 x !simulation.logic<1>>>, i32) -> !simulation.ref<!simulation.logic<1>>
// CHECK: simulation.ref.store %[[ONE]] to %[[BIT]]
// CHECK: call @observe(%[[TWO]])
// CHECK: %[[READ:.*]] = simulation.ref.load %[[LOW]] : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
// CHECK: %[[VALUE:.*]] = simulation.logic.to_bits %[[READ]] : !simulation.logic<8> -> i8
// CHECK: return %[[VALUE]]
func.func @storage_member(%s: !simulation.ref<!struct>, %b: !simulation.ref<!bits>, %part: i8, %index: i32) -> i8 {
  %true = arith.constant true
  %two = simulation.ref.subelement %s[[0]] : !simulation.ref<!struct> -> !simulation.ref<i8>
  simulation.ref.store %part to %two : i8, !simulation.ref<i8>
  %h = simulation.ref.subelement %b[[0]] : !simulation.ref<!bits> -> !simulation.ref<!simulation.packed_array<3 : 0 x i1>>
  %bit = simulation.ref.array_element %h[%index] : (!simulation.ref<!simulation.packed_array<3 : 0 x i1>>, i32) -> !simulation.ref<i1>
  simulation.ref.store %true to %bit : i1, !simulation.ref<i1>
  func.call @observe(%two) : (!simulation.ref<i8>) -> ()
  %v = simulation.ref.load %two : !simulation.ref<i8> -> i8
  return %v : i8
}

// Packed net slices compose and a whole-width view is the net itself. A view
// of an unpacked net element is addressed by storage offset and names exactly
// one element, so a slice of it does not compose with the element view.
// CHECK-LABEL: func.func @nets(
// CHECK-NEXT: %[[SLICE:.*]] = simulation.net.extract %arg0 from 3 : !simulation.net<!simulation.logic<8>> -> !simulation.net<!simulation.logic<2>>
// CHECK-NEXT: %[[ELEMENT:.*]] = simulation.net.extract %arg1 from 2
// CHECK-NEXT: %[[BIT:.*]] = simulation.net.extract %[[ELEMENT]] from 1
// CHECK-NEXT: return %[[SLICE]], %arg0, %[[BIT]]
func.func @nets(%net: !simulation.net<!simulation.logic<8>>, %array: !simulation.net<!array>) -> (!simulation.net<!simulation.logic<2>>, !simulation.net<!simulation.logic<8>>, !simulation.net<!simulation.logic<1>>) {
  %a = simulation.net.extract %net from 2 : !simulation.net<!simulation.logic<8>> -> !simulation.net<!simulation.logic<4>>
  %b = simulation.net.extract %a from 1 : !simulation.net<!simulation.logic<4>> -> !simulation.net<!simulation.logic<2>>
  %c = simulation.net.extract %net from 0 : !simulation.net<!simulation.logic<8>> -> !simulation.net<!simulation.logic<8>>
  %e = simulation.net.extract %array from 2 : !simulation.net<!array> -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
  %f = simulation.net.extract %e from 1 : !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> -> !simulation.net<!simulation.logic<1>>
  return %b, %c, %f : !simulation.net<!simulation.logic<2>>, !simulation.net<!simulation.logic<8>>, !simulation.net<!simulation.logic<1>>
}

// 7.4.6: writing back the element just read is a no-op, even for an invalid
// index, which reads the default and ignores the write.
// CHECK-LABEL: func.func @write_back(
// CHECK-NEXT: return %arg0
func.func @write_back(%array: !simulation.unpacked_array<3 : 0 x i8>, %index: !simulation.logic<4>) -> !simulation.unpacked_array<3 : 0 x i8> {
  %value = simulation.array.extract_dynamic %array[%index] : (!simulation.unpacked_array<3 : 0 x i8>, !simulation.logic<4>) -> i8
  %updated = simulation.array.insert_dynamic %value into %array[%index] : (!simulation.unpacked_array<3 : 0 x i8>, i8, !simulation.logic<4>) -> !simulation.unpacked_array<3 : 0 x i8>
  return %updated : !simulation.unpacked_array<3 : 0 x i8>
}

// 7.3.1: reading another member of a hard packed union reinterprets its
// bits; a two-state member of four-state bits converts X and Z to zero.
// CHECK-LABEL: func.func @union_reinterpret(
// CHECK-NEXT: %[[TWO:.*]] = simulation.logic.to_bits %arg0 : !simulation.logic<16> -> i16
// CHECK-NEXT: %[[FOUR:.*]] = simulation.logic.from_bits %arg1 : i16 -> !simulation.logic<16>
// CHECK-NEXT: return %[[TWO]], %[[FOUR]]
func.func @union_reinterpret(%four: !simulation.logic<16>, %two: i16) -> (i16, !simulation.logic<16>) {
  %u0 = simulation.union.construct %four as 0 : (!simulation.logic<16>) -> !mixed
  %r0 = simulation.union.extract %u0[1] : (!mixed) -> i16
  %u1 = simulation.union.construct %two as 1 : (i16) -> !mixed
  %r1 = simulation.union.extract %u1[0] : (!mixed) -> !simulation.logic<16>
  return %r0, %r1 : i16, !simulation.logic<16>
}

// 11.5.1: a two-state selection at constant index zero is a truncation.
// CHECK-LABEL: func.func @bits_zero_index(
// CHECK-NEXT: %[[T:.*]] = arith.trunci %arg0 : i16 to i8
// CHECK-NEXT: return %[[T]]
func.func @bits_zero_index(%bits: i16) -> i8 {
  %zero = arith.constant 0 : i32
  %r = simulation.bits.dyn_extract %bits from %zero : (i16, i32) -> i8
  return %r : i8
}
