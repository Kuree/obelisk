// RUN: obelisk-opt --allow-unregistered-dialect \
// RUN:   --convert-moore-to-obelisk %s \
// RUN:   | obelisk-opt --allow-unregistered-dialect | FileCheck %s

module {
  "test.types"() : () -> (
    !moore.void,
    !moore.l4,
    !moore.i8,
    !moore.f32,
    !moore.f64,
    !moore.time,
    !moore.string,
    !moore.chandle,
    !moore.event,
    !moore.null,
    !moore.class<@Packet>,
    !moore.array<3 x l2>,
    !moore.uarray<5 x !moore.string>,
    !moore.struct<{tag: l4, payload: l12}>,
    !moore.ustruct<{name: !moore.string, count: i32}>,
    !moore.union<{bits: l8, byte: i8}>,
    !moore.uunion<{text: !moore.string, count: i32}>,
    !moore.open_array<l8>,
    !moore.open_uarray<!moore.string>,
    !moore.assoc_array<!moore.string, i32>,
    !moore.queue<l16, 7>,
    !moore.ref<!moore.open_uarray<l8>>,
    !moore.format_string,
    !moore.scan_string
  )
}

// CHECK-LABEL: module {
// CHECK: "test.types"() : () -> (
// CHECK-SAME: !obelisk.void,
// CHECK-SAME: !obelisk.logic<4>,
// CHECK-SAME: i8,
// CHECK-SAME: f32,
// CHECK-SAME: f64,
// CHECK-SAME: !obelisk.time,
// CHECK-SAME: !sim.dstring,
// CHECK-SAME: !obelisk.chandle,
// CHECK-SAME: !obelisk.event,
// CHECK-SAME: !obelisk.null,
// CHECK-SAME: !obelisk.class_handle<@Packet>,
// CHECK-SAME: !obelisk.packed_array<3 x !obelisk.logic<2>>,
// CHECK-SAME: !obelisk.unpacked_array<5 x !sim.dstring>,
// CHECK-SAME: !obelisk.packed_struct<!hw.struct<tag: !obelisk.logic<4>, payload: !obelisk.logic<12>>>,
// CHECK-SAME: !obelisk.unpacked_struct<!hw.struct<name: !sim.dstring, count: i32>>,
// CHECK-SAME: !obelisk.packed_union<!hw.union<bits: !obelisk.logic<8>, byte: i8>>,
// CHECK-SAME: !obelisk.unpacked_union<!hw.union<text: !sim.dstring, count: i32>>,
// CHECK-SAME: !obelisk.open_array<!obelisk.logic<8>, true>,
// CHECK-SAME: !obelisk.open_array<!sim.dstring, false>,
// CHECK-SAME: !obelisk.assoc<i32, !sim.dstring>,
// CHECK-SAME: !sim.queue<!obelisk.logic<16>, 7>,
// CHECK-SAME: !obelisk.ref<!obelisk.open_array<!obelisk.logic<8>, false>>,
// CHECK-SAME: !sim.fstring,
// CHECK-SAME: !obelisk.scan_state
// CHECK-NOT: moore.
