// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LOWER

// Runtime behavior is checked in ../Runtime/simulation-container-bitstream-runtime.test.

// LOWER: llvm.call @obelisk_rt_v1_container_bitstream_link_anchor

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_bitstream_runtime {
    obelisk_sim.scope.decl 0 hierarchy "container_bitstream_runtime"
    obelisk_sim.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "container_bitstream_runtime.root"
    obelisk_sim.code_unit.decl 9910001 in 0 initial
        hierarchy "container_bitstream_runtime.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %two = arith.constant 2 : i64
      %three = arith.constant 3 : i64
      %a = obelisk_sim.logic.constant 12 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %b = obelisk_sim.logic.constant 13 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %c = obelisk_sim.logic.constant 14 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %source = obelisk_sim.container.create %three {
        type_id = 9910002 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>
      obelisk_sim.container.write %source, %zero, %a :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %source, %one, %b :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %source, %two, %c :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      %packed = obelisk_sim.container.export_bitstream %source :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>) ->
          !obelisk_sim.logic<24>
      %expected = obelisk_sim.logic.constant 789774 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %order_ok = obelisk_sim.logic.compare case_eq %packed, %expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1

      // A four-state stream converted to a two-state target maps X and Z to 0.
      %xz = obelisk_sim.logic.constant -1 : i8, 15 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.container.write %source, %zero, %xz :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>, i64,
           !obelisk_sim.logic<8>) -> ()
      %two_state = obelisk_sim.container.export_bitstream %source :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<8>>) -> i24
      %two_state_expected = arith.constant 15731982 : i24
      %xz_ok = arith.cmpi eq, %two_state, %two_state_expected : i24

      %bits = obelisk_sim.container.create %three {
        type_id = 9910003 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i8>
      %v0 = arith.constant 18 : i8
      %v1 = arith.constant 52 : i8
      %v2 = arith.constant 86 : i8
      obelisk_sim.container.write %bits, %zero, %v0 :
          (!obelisk_sim.dynamic_array<i8>, i64, i8) -> ()
      obelisk_sim.container.write %bits, %one, %v1 :
          (!obelisk_sim.dynamic_array<i8>, i64, i8) -> ()
      obelisk_sim.container.write %bits, %two, %v2 :
          (!obelisk_sim.dynamic_array<i8>, i64, i8) -> ()
      %four_state = obelisk_sim.container.export_bitstream %bits :
          (!obelisk_sim.dynamic_array<i8>) -> !obelisk_sim.logic<24>
      %four_state_expected = obelisk_sim.logic.constant 1193046 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %state_ok = obelisk_sim.logic.compare case_eq
          %four_state, %four_state_expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1

      // Shift a queue head far enough that the final append wraps physically.
      %queue = obelisk_sim.container.create %zero {
        type_id = 9910002 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 2 : i32, bound = -1 : i64
      } : (i64) -> !obelisk_sim.queue<!obelisk_sim.logic<8>, 0>
      obelisk_sim.container.write %queue, %zero, %a :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %queue, %one, %b :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.container.write %queue, %two, %c :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.queue.delete %queue[%zero] :
          !obelisk_sim.queue<!obelisk_sim.logic<8>, 0>
      %d = obelisk_sim.logic.constant 15 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.container.write %queue, %two, %d :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      obelisk_sim.queue.delete %queue[%zero] :
          !obelisk_sim.queue<!obelisk_sim.logic<8>, 0>
      obelisk_sim.container.write %queue, %two, %a :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>, i64,
           !obelisk_sim.logic<8>) -> ()
      %queue_packed = obelisk_sim.container.export_bitstream %queue :
          (!obelisk_sim.queue<!obelisk_sim.logic<8>, 0>) ->
          !obelisk_sim.logic<24>
      %queue_expected = obelisk_sim.logic.constant 921356 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %queue_ok = obelisk_sim.logic.compare case_eq
          %queue_packed, %queue_expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1

      // Associative values use index order, independently of insertion order.
      %assoc = obelisk_sim.assoc.create {
        type_id = 9910004 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = 2 : i32, key_width = 32 : i64
      } : () -> !obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>
      %negative = arith.constant -1 : i32
      %key_two = arith.constant 2 : i32
      %key_zero = arith.constant 0 : i32
      %aa = obelisk_sim.logic.constant 170 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %bb = obelisk_sim.logic.constant 187 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      %cc = obelisk_sim.logic.constant 204 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.assoc.write %assoc, %key_two, %cc :
          (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>,
           i32, !obelisk_sim.logic<8>) -> ()
      obelisk_sim.assoc.write %assoc, %negative, %aa :
          (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>,
           i32, !obelisk_sim.logic<8>) -> ()
      obelisk_sim.assoc.write %assoc, %key_zero, %bb :
          (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>,
           i32, !obelisk_sim.logic<8>) -> ()
      %assoc_packed = obelisk_sim.container.export_bitstream %assoc :
          (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>) ->
          !obelisk_sim.logic<24>
      %assoc_expected = obelisk_sim.logic.constant 11189196 : i24, 0 : i24 :
          !obelisk_sim.logic<24>
      %assoc_ok = obelisk_sim.logic.compare case_eq
          %assoc_packed, %assoc_expected :
          (!obelisk_sim.logic<24>, !obelisk_sim.logic<24>) -> i1

      // A two-state target maps an associative element's X/Z bits to zero.
      obelisk_sim.assoc.write %assoc, %negative, %xz :
          (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>,
           i32, !obelisk_sim.logic<8>) -> ()
      %assoc_bits = obelisk_sim.container.export_bitstream %assoc :
          (!obelisk_sim.assoc_array<i32, !obelisk_sim.logic<8>, true, false>) ->
          i24
      %assoc_bits_expected = arith.constant 15776716 : i24
      %assoc_xz_ok = arith.cmpi eq, %assoc_bits, %assoc_bits_expected : i24

      // String indices are ordered lexicographically, not by insertion order.
      %string_assoc = obelisk_sim.assoc.create {
        type_id = 9910005 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = 3 : i32, key_width = 0 : i64
      } : () -> !obelisk_sim.assoc_array<!obelisk_sim.string, i8, false, false>
      %z_key = obelisk_sim.string.literal "z"
      %aa_key = obelisk_sim.string.literal "aa"
      %z_value = arith.constant 34 : i8
      %aa_value = arith.constant 17 : i8
      obelisk_sim.assoc.write %string_assoc, %z_key, %z_value :
          (!obelisk_sim.assoc_array<!obelisk_sim.string, i8, false, false>,
           !obelisk_sim.string, i8) -> ()
      obelisk_sim.assoc.write %string_assoc, %aa_key, %aa_value :
          (!obelisk_sim.assoc_array<!obelisk_sim.string, i8, false, false>,
           !obelisk_sim.string, i8) -> ()
      %string_packed = obelisk_sim.container.export_bitstream %string_assoc :
          (!obelisk_sim.assoc_array<!obelisk_sim.string, i8, false, false>) ->
          i16
      %string_expected = arith.constant 4386 : i16
      %string_ok = arith.cmpi eq, %string_packed, %string_expected : i16

      // Wildcard indices use canonical unsigned numeric ordering rather than
      // allocation, insertion, or little-endian byte order. In particular, 1
      // sorts before the wider-than-a-byte value 256.
      %wildcard = obelisk_sim.assoc.create {
        type_id = 9910007 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = 6 : i32, key_width = 0 : i64
      } : () -> !obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>
      %key_storage_two = obelisk_sim.container.create %one {
        type_id = 9910008 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 2 : i64,
        alignment = 1 : i64, bit_width = 16 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i16>
      %key_storage_one = obelisk_sim.container.create %one {
        type_id = 9910008 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 2 : i64,
        alignment = 1 : i64, bit_width = 16 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i16>
      %key_wide = arith.constant 256 : i16
      %key_one_wide = arith.constant 1 : i16
      %wide_value = arith.constant 2 : i8
      obelisk_sim.container.write %key_storage_two, %zero, %key_wide :
          (!obelisk_sim.dynamic_array<i16>, i64, i16) -> ()
      obelisk_sim.container.write %key_storage_one, %zero, %key_one_wide :
          (!obelisk_sim.dynamic_array<i16>, i64, i16) -> ()
      %wildcard_key_two = obelisk_sim.box.pack %key_storage_two :
          (!obelisk_sim.dynamic_array<i16>) -> !obelisk_sim.box
      %wildcard_key_one = obelisk_sim.box.pack %key_storage_one :
          (!obelisk_sim.dynamic_array<i16>) -> !obelisk_sim.box
      obelisk_sim.assoc.write %wildcard, %wildcard_key_two, %wide_value :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>,
           !obelisk_sim.box, i8) -> ()
      obelisk_sim.assoc.write %wildcard, %wildcard_key_one, %aa_value :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>,
           !obelisk_sim.box, i8) -> ()
      %unknown_storage = obelisk_sim.container.create %one {
        type_id = 9910009 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 4 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
      %unknown_value = obelisk_sim.logic.constant 3 : i4, 1 : i4 :
          !obelisk_sim.logic<4>
      obelisk_sim.container.write %unknown_storage, %zero, %unknown_value :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>, i64,
           !obelisk_sim.logic<4>) -> ()
      %unknown_key = obelisk_sim.box.pack %unknown_storage :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>) ->
          !obelisk_sim.box
      obelisk_sim.assoc.write %wildcard, %unknown_key, %aa_value :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>,
           !obelisk_sim.box, i8) -> ()
      %wildcard_packed = obelisk_sim.container.export_bitstream %wildcard :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>) -> i16
      %wildcard_expected = arith.constant 4354 : i16
      %wildcard_values_ok = arith.cmpi eq, %wildcard_packed,
          %wildcard_expected : i16
      %wildcard_size = obelisk_sim.container.size %wildcard :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>) -> i64
      %wildcard_size_ok = arith.cmpi eq, %wildcard_size, %two : i64
      %wildcard_ok = arith.andi %wildcard_values_ok, %wildcard_size_ok : i1

      // IEEE 1800-2017 7.8.1 removes leading zeroes from wildcard indices.
      // The same numeric value must therefore match across original widths
      // and element descriptors.
      %canonical_wildcard = obelisk_sim.assoc.create {
        type_id = 9910010 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = 6 : i32, key_width = 0 : i64
      } : () -> !obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>
      %minimal_key_storage = obelisk_sim.container.create %one {
        type_id = 9910011 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 3 : i64,
        alignment = 1 : i64, bit_width = 23 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i23>
      %leading_zero_key_storage = obelisk_sim.container.create %one {
        type_id = 9910012 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 3 : i64,
        alignment = 1 : i64, bit_width = 24 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<i24>
      %minimal_key_value = arith.constant 4408131 : i23
      %leading_zero_key_value = arith.constant 4408131 : i24
      obelisk_sim.container.write %minimal_key_storage, %zero,
          %minimal_key_value :
          (!obelisk_sim.dynamic_array<i23>, i64, i23) -> ()
      obelisk_sim.container.write %leading_zero_key_storage, %zero,
          %leading_zero_key_value :
          (!obelisk_sim.dynamic_array<i24>, i64, i24) -> ()
      %minimal_key = obelisk_sim.box.pack %minimal_key_storage :
          (!obelisk_sim.dynamic_array<i23>) -> !obelisk_sim.box
      %leading_zero_key = obelisk_sim.box.pack %leading_zero_key_storage :
          (!obelisk_sim.dynamic_array<i24>) -> !obelisk_sim.box
      obelisk_sim.assoc.write %canonical_wildcard, %minimal_key, %wide_value :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>,
           !obelisk_sim.box, i8) -> ()
      %canonical_read = obelisk_sim.assoc.read %canonical_wildcard,
          %leading_zero_key :
          (!obelisk_sim.assoc_array<!obelisk_sim.box, i8, false, true>,
           !obelisk_sim.box) -> i8
      %canonical_ok = arith.cmpi eq, %canonical_read, %wide_value : i8

      // At the 3/4 load threshold, overwriting a value preserves the sorted
      // key cache and must not be mistaken for an insertion that needs growth.
      %threshold = obelisk_sim.assoc.create {
        type_id = 9910006 : i64, element_kind = 1 : i32,
        element_flags = 0 : i32, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = 1 : i32, key_width = 32 : i64
      } : () -> !obelisk_sim.assoc_array<i32, i8, false, false>
      %key_one = arith.constant 1 : i32
      %key_three = arith.constant 3 : i32
      %key_four = arith.constant 4 : i32
      %key_five = arith.constant 5 : i32
      %value_one = arith.constant 1 : i8
      %value_two = arith.constant 2 : i8
      %value_three = arith.constant 3 : i8
      %value_four = arith.constant 4 : i8
      %value_five = arith.constant 5 : i8
      %value_six = arith.constant 6 : i8
      obelisk_sim.assoc.write %threshold, %key_five, %value_six :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      obelisk_sim.assoc.write %threshold, %key_zero, %value_one :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      obelisk_sim.assoc.write %threshold, %key_three, %value_four :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      obelisk_sim.assoc.write %threshold, %key_one, %value_two :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      obelisk_sim.assoc.write %threshold, %key_four, %value_five :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      obelisk_sim.assoc.write %threshold, %key_two, %value_three :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      %threshold_before = obelisk_sim.container.export_bitstream %threshold :
          (!obelisk_sim.assoc_array<i32, i8, false, false>) -> i48
      %threshold_expected = arith.constant 1108152157446 : i48
      %threshold_ok = arith.cmpi eq, %threshold_before, %threshold_expected : i48
      %replacement = arith.constant 170 : i8
      obelisk_sim.assoc.write %threshold, %key_zero, %replacement :
          (!obelisk_sim.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      %threshold_after = obelisk_sim.container.export_bitstream %threshold :
          (!obelisk_sim.assoc_array<i32, i8, false, false>) -> i48
      %replacement_expected = arith.constant 186925617251590 : i48
      %replacement_ok = arith.cmpi eq, %threshold_after,
          %replacement_expected : i48

      %format = obelisk_sim.bytes.constant
          "%0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %format, %order_ok, %xz_ok, %state_ok, %queue_ok, %assoc_ok,
          %assoc_xz_ok, %string_ok, %wildcard_ok, %canonical_ok, %threshold_ok,
          %replacement_ok)
          newline = true radix = 10
          flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1
      obelisk_sim.return
    }
  }
}
