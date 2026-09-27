// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=LOWER

// Runtime behavior is checked in ../Runtime/simulation-container-bitstream-runtime.test.

// LOWER: llvm.call @obelisk_rt_v1_container_bitstream_link_anchor

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @container_bitstream_runtime {
    simulation.scope.decl 0 hierarchy "container_bitstream_runtime"
    simulation.code_unit.decl 9910000 in 0 root_initializer
        hierarchy "container_bitstream_runtime.root"
    simulation.code_unit.decl 9910001 in 0 initial
        hierarchy "container_bitstream_runtime.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9910000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9910001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %two = arith.constant 2 : i64
      %three = arith.constant 3 : i64
      %a = simulation.logic.constant 12 : i8, 0 : i8 :
          !simulation.logic<8>
      %b = simulation.logic.constant 13 : i8, 0 : i8 :
          !simulation.logic<8>
      %c = simulation.logic.constant 14 : i8, 0 : i8 :
          !simulation.logic<8>
      %source = simulation.container.create %three {
        type_id = 9910002 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!simulation.logic<8>>
      simulation.container.write %source, %zero, %a :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %source, %one, %b :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %source, %two, %c :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      %packed = simulation.container.export_bitstream %source :
          (!simulation.dynamic_array<!simulation.logic<8>>) ->
          !simulation.logic<24>
      %expected = simulation.logic.constant 789774 : i24, 0 : i24 :
          !simulation.logic<24>
      %order_ok = simulation.logic.compare case_eq %packed, %expected :
          (!simulation.logic<24>, !simulation.logic<24>) -> i1

      // A four-state stream converted to a two-state target maps X and Z to 0.
      %xz = simulation.logic.constant -1 : i8, 15 : i8 :
          !simulation.logic<8>
      simulation.container.write %source, %zero, %xz :
          (!simulation.dynamic_array<!simulation.logic<8>>, i64,
           !simulation.logic<8>) -> ()
      %two_state = simulation.container.export_bitstream %source :
          (!simulation.dynamic_array<!simulation.logic<8>>) -> i24
      %two_state_expected = arith.constant 15731982 : i24
      %xz_ok = arith.cmpi eq, %two_state, %two_state_expected : i24

      %bits = simulation.container.create %three {
        type_id = 9910003 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i8>
      %v0 = arith.constant 18 : i8
      %v1 = arith.constant 52 : i8
      %v2 = arith.constant 86 : i8
      simulation.container.write %bits, %zero, %v0 :
          (!simulation.dynamic_array<i8>, i64, i8) -> ()
      simulation.container.write %bits, %one, %v1 :
          (!simulation.dynamic_array<i8>, i64, i8) -> ()
      simulation.container.write %bits, %two, %v2 :
          (!simulation.dynamic_array<i8>, i64, i8) -> ()
      %four_state = simulation.container.export_bitstream %bits :
          (!simulation.dynamic_array<i8>) -> !simulation.logic<24>
      %four_state_expected = simulation.logic.constant 1193046 : i24, 0 : i24 :
          !simulation.logic<24>
      %state_ok = simulation.logic.compare case_eq
          %four_state, %four_state_expected :
          (!simulation.logic<24>, !simulation.logic<24>) -> i1

      // Shift a queue head far enough that the final append wraps physically.
      %queue = simulation.container.create %zero {
        type_id = 9910002 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<queue>, bound = -1 : i64
      } : (i64) -> !simulation.queue<!simulation.logic<8>, 0>
      simulation.container.write %queue, %zero, %a :
          (!simulation.queue<!simulation.logic<8>, 0>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %queue, %one, %b :
          (!simulation.queue<!simulation.logic<8>, 0>, i64,
           !simulation.logic<8>) -> ()
      simulation.container.write %queue, %two, %c :
          (!simulation.queue<!simulation.logic<8>, 0>, i64,
           !simulation.logic<8>) -> ()
      simulation.queue.delete %queue[%zero] :
          !simulation.queue<!simulation.logic<8>, 0>
      %d = simulation.logic.constant 15 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.container.write %queue, %two, %d :
          (!simulation.queue<!simulation.logic<8>, 0>, i64,
           !simulation.logic<8>) -> ()
      simulation.queue.delete %queue[%zero] :
          !simulation.queue<!simulation.logic<8>, 0>
      simulation.container.write %queue, %two, %a :
          (!simulation.queue<!simulation.logic<8>, 0>, i64,
           !simulation.logic<8>) -> ()
      %queue_packed = simulation.container.export_bitstream %queue :
          (!simulation.queue<!simulation.logic<8>, 0>) ->
          !simulation.logic<24>
      %queue_expected = simulation.logic.constant 921356 : i24, 0 : i24 :
          !simulation.logic<24>
      %queue_ok = simulation.logic.compare case_eq
          %queue_packed, %queue_expected :
          (!simulation.logic<24>, !simulation.logic<24>) -> i1

      // Associative values use index order, independently of insertion order.
      %assoc = simulation.assoc.create {
        type_id = 9910004 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = #simulation.assoc_key_kind<signed>, key_width = 32 : i64
      } : () -> !simulation.assoc_array<i32, !simulation.logic<8>, true, false>
      %negative = arith.constant -1 : i32
      %key_two = arith.constant 2 : i32
      %key_zero = arith.constant 0 : i32
      %aa = simulation.logic.constant 170 : i8, 0 : i8 :
          !simulation.logic<8>
      %bb = simulation.logic.constant 187 : i8, 0 : i8 :
          !simulation.logic<8>
      %cc = simulation.logic.constant 204 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.assoc.write %assoc, %key_two, %cc :
          (!simulation.assoc_array<i32, !simulation.logic<8>, true, false>,
           i32, !simulation.logic<8>) -> ()
      simulation.assoc.write %assoc, %negative, %aa :
          (!simulation.assoc_array<i32, !simulation.logic<8>, true, false>,
           i32, !simulation.logic<8>) -> ()
      simulation.assoc.write %assoc, %key_zero, %bb :
          (!simulation.assoc_array<i32, !simulation.logic<8>, true, false>,
           i32, !simulation.logic<8>) -> ()
      %assoc_packed = simulation.container.export_bitstream %assoc :
          (!simulation.assoc_array<i32, !simulation.logic<8>, true, false>) ->
          !simulation.logic<24>
      %assoc_expected = simulation.logic.constant 11189196 : i24, 0 : i24 :
          !simulation.logic<24>
      %assoc_ok = simulation.logic.compare case_eq
          %assoc_packed, %assoc_expected :
          (!simulation.logic<24>, !simulation.logic<24>) -> i1

      // A two-state target maps an associative element's X/Z bits to zero.
      simulation.assoc.write %assoc, %negative, %xz :
          (!simulation.assoc_array<i32, !simulation.logic<8>, true, false>,
           i32, !simulation.logic<8>) -> ()
      %assoc_bits = simulation.container.export_bitstream %assoc :
          (!simulation.assoc_array<i32, !simulation.logic<8>, true, false>) ->
          i24
      %assoc_bits_expected = arith.constant 15776716 : i24
      %assoc_xz_ok = arith.cmpi eq, %assoc_bits, %assoc_bits_expected : i24

      // String indices are ordered lexicographically, not by insertion order.
      %string_assoc = simulation.assoc.create {
        type_id = 9910005 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = #simulation.assoc_key_kind<string>, key_width = 0 : i64
      } : () -> !simulation.assoc_array<!simulation.string, i8, false, false>
      %z_key = simulation.string.literal "z"
      %aa_key = simulation.string.literal "aa"
      %z_value = arith.constant 34 : i8
      %aa_value = arith.constant 17 : i8
      simulation.assoc.write %string_assoc, %z_key, %z_value :
          (!simulation.assoc_array<!simulation.string, i8, false, false>,
           !simulation.string, i8) -> ()
      simulation.assoc.write %string_assoc, %aa_key, %aa_value :
          (!simulation.assoc_array<!simulation.string, i8, false, false>,
           !simulation.string, i8) -> ()
      %string_packed = simulation.container.export_bitstream %string_assoc :
          (!simulation.assoc_array<!simulation.string, i8, false, false>) ->
          i16
      %string_expected = arith.constant 4386 : i16
      %string_ok = arith.cmpi eq, %string_packed, %string_expected : i16

      // Wildcard indices use canonical unsigned numeric ordering rather than
      // allocation, insertion, or little-endian byte order. In particular, 1
      // sorts before the wider-than-a-byte value 256.
      %wildcard = simulation.assoc.create {
        type_id = 9910007 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = #simulation.assoc_key_kind<wildcard>, key_width = 0 : i64
      } : () -> !simulation.assoc_array<!simulation.box, i8, false, true>
      %key_storage_two = simulation.container.create %one {
        type_id = 9910008 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 2 : i64,
        alignment = 1 : i64, bit_width = 16 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i16>
      %key_storage_one = simulation.container.create %one {
        type_id = 9910008 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 2 : i64,
        alignment = 1 : i64, bit_width = 16 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i16>
      %key_wide = arith.constant 256 : i16
      %key_one_wide = arith.constant 1 : i16
      %wide_value = arith.constant 2 : i8
      simulation.container.write %key_storage_two, %zero, %key_wide :
          (!simulation.dynamic_array<i16>, i64, i16) -> ()
      simulation.container.write %key_storage_one, %zero, %key_one_wide :
          (!simulation.dynamic_array<i16>, i64, i16) -> ()
      %wildcard_key_two = simulation.box.pack %key_storage_two :
          (!simulation.dynamic_array<i16>) -> !simulation.box
      %wildcard_key_one = simulation.box.pack %key_storage_one :
          (!simulation.dynamic_array<i16>) -> !simulation.box
      simulation.assoc.write %wildcard, %wildcard_key_two, %wide_value :
          (!simulation.assoc_array<!simulation.box, i8, false, true>,
           !simulation.box, i8) -> ()
      simulation.assoc.write %wildcard, %wildcard_key_one, %aa_value :
          (!simulation.assoc_array<!simulation.box, i8, false, true>,
           !simulation.box, i8) -> ()
      %unknown_storage = simulation.container.create %one {
        type_id = 9910009 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 4 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!simulation.logic<4>>
      %unknown_value = simulation.logic.constant 3 : i4, 1 : i4 :
          !simulation.logic<4>
      simulation.container.write %unknown_storage, %zero, %unknown_value :
          (!simulation.dynamic_array<!simulation.logic<4>>, i64,
           !simulation.logic<4>) -> ()
      %unknown_key = simulation.box.pack %unknown_storage :
          (!simulation.dynamic_array<!simulation.logic<4>>) ->
          !simulation.box
      simulation.assoc.write %wildcard, %unknown_key, %aa_value :
          (!simulation.assoc_array<!simulation.box, i8, false, true>,
           !simulation.box, i8) -> ()
      %wildcard_packed = simulation.container.export_bitstream %wildcard :
          (!simulation.assoc_array<!simulation.box, i8, false, true>) -> i16
      %wildcard_expected = arith.constant 4354 : i16
      %wildcard_values_ok = arith.cmpi eq, %wildcard_packed,
          %wildcard_expected : i16
      %wildcard_size = simulation.container.size %wildcard :
          (!simulation.assoc_array<!simulation.box, i8, false, true>) -> i64
      %wildcard_size_ok = arith.cmpi eq, %wildcard_size, %two : i64
      %wildcard_ok = arith.andi %wildcard_values_ok, %wildcard_size_ok : i1

      // IEEE 1800-2017 7.8.1 removes leading zeroes from wildcard indices.
      // The same numeric value must therefore match across original widths
      // and element descriptors.
      %canonical_wildcard = simulation.assoc.create {
        type_id = 9910010 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = #simulation.assoc_key_kind<wildcard>, key_width = 0 : i64
      } : () -> !simulation.assoc_array<!simulation.box, i8, false, true>
      %minimal_key_storage = simulation.container.create %one {
        type_id = 9910011 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 3 : i64,
        alignment = 1 : i64, bit_width = 23 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i23>
      %leading_zero_key_storage = simulation.container.create %one {
        type_id = 9910012 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 3 : i64,
        alignment = 1 : i64, bit_width = 24 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<i24>
      %minimal_key_value = arith.constant 4408131 : i23
      %leading_zero_key_value = arith.constant 4408131 : i24
      simulation.container.write %minimal_key_storage, %zero,
          %minimal_key_value :
          (!simulation.dynamic_array<i23>, i64, i23) -> ()
      simulation.container.write %leading_zero_key_storage, %zero,
          %leading_zero_key_value :
          (!simulation.dynamic_array<i24>, i64, i24) -> ()
      %minimal_key = simulation.box.pack %minimal_key_storage :
          (!simulation.dynamic_array<i23>) -> !simulation.box
      %leading_zero_key = simulation.box.pack %leading_zero_key_storage :
          (!simulation.dynamic_array<i24>) -> !simulation.box
      simulation.assoc.write %canonical_wildcard, %minimal_key, %wide_value :
          (!simulation.assoc_array<!simulation.box, i8, false, true>,
           !simulation.box, i8) -> ()
      %canonical_read = simulation.assoc.read %canonical_wildcard,
          %leading_zero_key :
          (!simulation.assoc_array<!simulation.box, i8, false, true>,
           !simulation.box) -> i8
      %canonical_ok = arith.cmpi eq, %canonical_read, %wide_value : i8

      // At the 3/4 load threshold, overwriting a value preserves the sorted
      // key cache and must not be mistaken for an insertion that needs growth.
      %threshold = simulation.assoc.create {
        type_id = 9910006 : i64, element_kind = #simulation.element_kind<bits>,
        element_flags = #simulation.element_flags<none>, value_size = 1 : i64,
        alignment = 1 : i64, bit_width = 8 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        key_kind = #simulation.assoc_key_kind<unsigned>, key_width = 32 : i64
      } : () -> !simulation.assoc_array<i32, i8, false, false>
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
      simulation.assoc.write %threshold, %key_five, %value_six :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %threshold, %key_zero, %value_one :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %threshold, %key_three, %value_four :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %threshold, %key_one, %value_two :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %threshold, %key_four, %value_five :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      simulation.assoc.write %threshold, %key_two, %value_three :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      %threshold_before = simulation.container.export_bitstream %threshold :
          (!simulation.assoc_array<i32, i8, false, false>) -> i48
      %threshold_expected = arith.constant 1108152157446 : i48
      %threshold_ok = arith.cmpi eq, %threshold_before, %threshold_expected : i48
      %replacement = arith.constant 170 : i8
      simulation.assoc.write %threshold, %key_zero, %replacement :
          (!simulation.assoc_array<i32, i8, false, false>, i32, i8) -> ()
      %threshold_after = simulation.container.export_bitstream %threshold :
          (!simulation.assoc_array<i32, i8, false, false>) -> i48
      %replacement_expected = arith.constant 186925617251590 : i48
      %replacement_ok = arith.cmpi eq, %threshold_after,
          %replacement_expected : i48

      %format = simulation.bytes.constant
          "%0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %order_ok, %xz_ok, %state_ok, %queue_ok, %assoc_ok,
          %assoc_xz_ok, %string_ok, %wildcard_ok, %canonical_ok, %threshold_ok,
          %replacement_ok)
          newline = true radix = <decimal>
          flags = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] :
          !simulation.bytes, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1, i1
      simulation.return
    }
  }
}
