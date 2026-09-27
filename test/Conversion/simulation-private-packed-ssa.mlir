// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' -o %t.off
// RUN: FileCheck %s --check-prefix=OFF < %t.off
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=READ
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=FULL

// Runtime behavior is checked in ../Runtime/simulation-private-packed-ssa.test.

// Forward whole values and nested packed views without converting their value
// domain. Two unknown bits straddle the native word boundary: one X and one Z.
// The bytecode reference is encoded from the original storage-based program.
// The dynamic spawn test uses the automatic event-loop policy after checking
// the promotion pass; its activation multiplicity cannot use an AOT plan.
!lanes = !simulation.packed_array<9 : 8 x !simulation.logic<65>>
!record = !simulation.packed_struct<[
  #simulation.field<name = "lanes", type = !lanes, ordinal = 0, packedOffset = 4>,
  #simulation.field<name = "tag", type = i4, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @private_packed_ssa {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "whole"
    simulation.code_unit.decl 3 in 0 function hierarchy "wide_bits"
    simulation.code_unit.decl 4 in 0 function hierarchy "packed"
    simulation.code_unit.decl 5 in 0 function hierarchy "read_before_write"
    simulation.code_unit.decl 6 in 0 function hierarchy "dynamic"
    simulation.code_unit.decl 7 in 0 function hierarchy "recursive"
    simulation.code_unit.decl 8 in 0 initial hierarchy "drive"
    simulation.code_unit.decl 9 in 0 initial hierarchy "suspended"
    simulation.storage.decl 0 in 0 : !simulation.logic<130> static
    simulation.storage.decl 1 in 0 : i130 static
    simulation.storage.decl 2 in 0 : !record static
    simulation.storage.decl 3 in 0 : !simulation.logic<130> static
    simulation.storage.decl 4 in 0 : !simulation.logic<130> static
    simulation.storage.decl 5 in 0 : !simulation.logic<130> static
    simulation.storage.decl 6 in 0 : !simulation.logic<130> static

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %xz = simulation.logic.constant 9223372036854775808 : i130, 27670116110564327424 : i130 : !simulation.logic<130>
      %whole = simulation.call @whole(%ctx, %xz) : (!simulation.context, !simulation.logic<130>) -> !simulation.logic<130>
      %whole_ok = simulation.logic.compare case_eq %whole, %xz : (!simulation.logic<130>, !simulation.logic<130>) -> i1
      %bits = arith.constant 680564733841876926926749214863536422917 : i130
      %wide = simulation.call @wide_bits(%ctx, %bits) : (!simulation.context, i130) -> i130
      %wide_ok = arith.cmpi eq, %wide, %bits : i130
      %lane = simulation.logic.constant 9223372036854775808 : i65, 27670116110564327424 : i65 : !simulation.logic<65>
      %other = simulation.logic.constant 7 : i65, 0 : i65 : !simulation.logic<65>
      %lanes = simulation.aggregate.construct %other, %lane : (!simulation.logic<65>, !simulation.logic<65>) -> !lanes
      %tag = arith.constant 11 : i4
      %record = simulation.aggregate.construct %lanes, %tag : (!lanes, i4) -> !record
      %packed_ok = simulation.call @packed(%ctx, %record, %lane, %tag) : (!simulation.context, !record, !simulation.logic<65>, i4) -> i1
      %again = arith.constant true
      %recursive = simulation.call @recursive(%ctx, %xz, %again) : (!simulation.context, !simulation.logic<130>, i1) -> !simulation.logic<130>
      %zero = simulation.logic.constant 0 : i130, 0 : i130 : !simulation.logic<130>
      %recursive_ok = simulation.logic.compare case_eq %recursive, %zero : (!simulation.logic<130>, !simulation.logic<130>) -> i1
      %fmt = simulation.bytes.constant "private SSA %0d %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %whole_ok, %wide_ok, %packed_ok, %recursive_ok) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, i1, i1
      %driver = simulation.spawn @drive(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    // OFF-LABEL: simulation.func private @whole(
    // OFF-NOT: simulation.ref.
    // OFF: simulation.return %arg1
    // READ-LABEL: simulation.func private @whole(
    // READ: simulation.ref.store {{.*}}schedule.eval.discardable_store
    // READ-NOT: simulation.ref.load
    // READ: simulation.return %arg1
    // FULL-LABEL: simulation.func private @whole(
    // FULL: simulation.ref.store
    // FULL: simulation.ref.load
    simulation.func private @whole(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<130> {simulation.capture_kind = 1 : i32}) -> !simulation.logic<130>
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %tmp = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<130>>
      simulation.ref.store %value to %tmp : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.br ^read
    ^read:
      %loaded = simulation.ref.load %tmp : !simulation.ref<!simulation.logic<130>> -> !simulation.logic<130>
      simulation.return %loaded : !simulation.logic<130>
    }

    // OFF-LABEL: simulation.func private @wide_bits(
    // OFF-NOT: simulation.ref.
    // OFF: simulation.return %arg1
    simulation.func private @wide_bits(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i130 {simulation.capture_kind = 1 : i32}) -> i130
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %tmp = simulation.context.storage %ctx[1] : !simulation.ref<i130>
      simulation.ref.store %value to %tmp : i130, !simulation.ref<i130>
      %loaded = simulation.ref.load %tmp : !simulation.ref<i130> -> i130
      simulation.return %loaded : i130
    }

    // OFF-LABEL: simulation.func private @packed(
    // OFF-NOT: simulation.ref.
    // OFF: %[[LANES:.*]] = simulation.aggregate.extract %arg1[0]
    // OFF-NEXT: %[[LANE:.*]] = simulation.aggregate.extract %[[LANES]][1]
    // OFF-NEXT: %[[TAG:.*]] = simulation.aggregate.extract %arg1[1]
    // OFF: simulation.logic.compare case_eq %[[LANE]], %arg2
    // OFF: arith.cmpi eq, %[[TAG]], %arg3
    // OFF-NOT: simulation.ref.
    // OFF: simulation.return
    // READ-LABEL: simulation.func private @packed(
    // READ: simulation.ref.store {{.*}}schedule.eval.discardable_store
    // READ-NOT: simulation.ref.load
    // READ: simulation.aggregate.extract
    // READ-NOT: simulation.ref.load
    // READ: simulation.return
    simulation.func private @packed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !record {simulation.capture_kind = 1 : i32},
        %expected: !simulation.logic<65> {simulation.capture_kind = 1 : i32},
        %tag: i4 {simulation.capture_kind = 1 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %tmp = simulation.context.storage %ctx[2] : !simulation.ref<!record>
      simulation.ref.store %value to %tmp : !record, !simulation.ref<!record>
      %lanes = simulation.ref.subelement %tmp[[0]] : !simulation.ref<!record> -> !simulation.ref<!lanes>
      %lane = simulation.ref.subelement %lanes[[1]] : !simulation.ref<!lanes> -> !simulation.ref<!simulation.logic<65>>
      %tag_ref = simulation.ref.subelement %tmp[[1]] : !simulation.ref<!record> -> !simulation.ref<i4>
      %loaded = simulation.ref.load %lane : !simulation.ref<!simulation.logic<65>> -> !simulation.logic<65>
      %loaded_tag = simulation.ref.load %tag_ref : !simulation.ref<i4> -> i4
      %same = simulation.logic.compare case_eq %loaded, %expected : (!simulation.logic<65>, !simulation.logic<65>) -> i1
      %tag_ok = arith.cmpi eq, %loaded_tag, %tag : i4
      %result = arith.andi %same, %tag_ok : i1
      simulation.return %result : i1
    }

    // OFF-LABEL: simulation.func private @read_before_write(
    // OFF: simulation.ref.load
    // OFF: simulation.ref.store
    // OFF: simulation.return
    simulation.func private @read_before_write(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<130> {simulation.capture_kind = 1 : i32}) -> !simulation.logic<130>
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %tmp = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<130>>
      %old = simulation.ref.load %tmp : !simulation.ref<!simulation.logic<130>> -> !simulation.logic<130>
      simulation.ref.store %value to %tmp : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      simulation.return %old : !simulation.logic<130>
    }

    // OFF-LABEL: simulation.func private @dynamic(
    // OFF: simulation.ref.store
    // OFF: simulation.ref.dyn_extract
    // OFF: simulation.ref.load
    simulation.func private @dynamic(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<130> {simulation.capture_kind = 1 : i32},
        %index: i32 {simulation.capture_kind = 1 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %tmp = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<130>>
      simulation.ref.store %value to %tmp : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      %part = simulation.ref.dyn_extract %tmp from %index : (!simulation.ref<!simulation.logic<130>>, i32) -> !simulation.ref<!simulation.logic<8>>
      %loaded = simulation.ref.load %part : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %loaded : !simulation.logic<8>
    }

    // Reentry writes the same static root through the same accessor. The
    // outer return must see the inner value, despite the dominating store.
    // OFF-LABEL: simulation.func private @recursive(
    // OFF: simulation.ref.store
    // OFF: simulation.call @recursive
    // OFF: simulation.ref.load
    simulation.func private @recursive(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<130> {simulation.capture_kind = 1 : i32},
        %again: i1 {simulation.capture_kind = 1 : i32}) -> !simulation.logic<130>
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %tmp = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<130>>
      simulation.ref.store %value to %tmp : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      cf.cond_br %again, ^reenter, ^done
    ^reenter:
      %zero = simulation.logic.constant 0 : i130, 0 : i130 : !simulation.logic<130>
      %stop = arith.constant false
      %ignored = simulation.call @recursive(%ctx, %zero, %stop) : (!simulation.context, !simulation.logic<130>, i1) -> !simulation.logic<130>
      cf.br ^done
    ^done:
      %loaded = simulation.ref.load %tmp : !simulation.ref<!simulation.logic<130>> -> !simulation.logic<130>
      simulation.return %loaded : !simulation.logic<130>
    }

    // Two invocations share the same static root. Stores occur at distinct
    // times (0 and 1), and both reads occur after the second store (2 and 3).
    // This is race-free and must preserve the update across each suspension.
    simulation.func private @drive(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %one = simulation.logic.constant 1 : i130, 0 : i130 : !simulation.logic<130>
      %first = simulation.spawn @suspended(%ctx, %one) : !simulation.context, !simulation.logic<130> -> !simulation.process
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^second
    ^second:
      %two = simulation.logic.constant 2 : i130, 0 : i130 : !simulation.logic<130>
      %second = simulation.spawn @suspended(%ctx, %two) : !simulation.context, !simulation.logic<130> -> !simulation.process
      simulation.return
    }

    // OFF-LABEL: simulation.func private @suspended(
    // OFF: simulation.ref.store
    // OFF: simulation.suspend.delay
    // OFF: simulation.ref.load
    simulation.func private @suspended(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.logic<130> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %tmp = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<130>>
      simulation.ref.store %value to %tmp : !simulation.logic<130>, !simulation.ref<!simulation.logic<130>>
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^read
    ^read:
      %loaded = simulation.ref.load %tmp : !simulation.ref<!simulation.logic<130>> -> !simulation.logic<130>
      %fmt = simulation.bytes.constant "suspended %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %loaded) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<130>
      simulation.return
    }
  }
}
