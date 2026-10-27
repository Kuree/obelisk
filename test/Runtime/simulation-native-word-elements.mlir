// RUN: talos-opt %s --schedule-optimize-native-words -o %t.words
// RUN: FileCheck %s --check-prefix=WORDS < %t.words
// RUN: talos-opt %t.words --convert-schedule-word-regions --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe | FileCheck %s

// LRM 7.4.5 and 11.4.8: 16 independent byte elements become two native
// 64-bit iterations. Reversal changes byte order, not the bits within a byte.
// The integer case also checks that a uniform byte is replicated, not extended.
// WORDS-LABEL: simulation.func @logic_bytes
// WORDS: schedule.word.region 64
// WORDS: schedule.word.extract {{.*}}!simulation.logic<64>
// WORDS: schedule.word.reverse {{.*}}lane_width = 8
// WORDS: simulation.logic.binary xor {{.*}}!simulation.logic<64>
// WORDS-LABEL: simulation.func @integer_bytes
// WORDS: schedule.word.region 64
// WORDS: schedule.word.extract {{.*}}i64
// WORDS: arith.xori {{.*}} : i64
// A one-bit logical negation is a bitwise lane negation (Table 11-15), so its
// word form is one bitwise negation rather than a truth table.
// WORDS: simulation.logic.unary bit_not {{.*}}!simulation.logic<8>
// WORDS: simulation.logic.replicate {{.*}}!simulation.logic<24>
// CHECK: byte lanes 1 1 1 1
!byte = !simulation.logic<8>
!src = !simulation.unpacked_array<15 : 0 x !simulation.logic<8>>
!dst = !simulation.packed_array<15 : 0 x !simulation.logic<8>>
!isrc = !simulation.unpacked_array<0 : 15 x i8>
!idst = !simulation.packed_array<15 : 0 x i8>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @bytes attributes {
    schedule.nba.transient_observable = array<i64>,
    schedule.nba.private_influence = array<i64: 1, 3, 5, 7>,
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [], regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>} {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "logic_bytes"
    simulation.code_unit.decl 3 in 0 function hierarchy "integer_bytes"
    simulation.storage.decl 0 in 0 : !src design
    simulation.storage.decl 1 in 0 : !dst design
    simulation.storage.decl 2 in 0 : !isrc design
    simulation.storage.decl 3 in 0 : !idst design
    simulation.storage.decl 4 in 0 : !simulation.logic<16> design
    simulation.storage.decl 5 in 0 : !simulation.logic<16> design
    simulation.storage.decl 6 in 0 : !simulation.logic<3> design
    simulation.storage.decl 7 in 0 : !simulation.packed_array<15 : 0 x !simulation.logic<3>> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!src>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!dst>
      %isrc = simulation.context.storage %ctx[2] : !simulation.ref<!isrc>
      %idst = simulation.context.storage %ctx[3] : !simulation.ref<!idst>

      %flags = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<16>>
      %out_flags = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<16>>
      %flags_value = simulation.logic.constant 43690 : i16, 52428 : i16 : !simulation.logic<16>
      simulation.ref.store %flags_value to %flags : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      %triple = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<3>>
      %out_triples = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<3>>>
      %triple_value = simulation.logic.constant 5 : i3, 2 : i3 : !simulation.logic<3>
      simulation.ref.store %triple_value to %triple : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      %v0 = simulation.logic.constant 3 : i8, 0 : i8 : !byte
      %i0 = arith.constant 3 : i8
      %v1 = simulation.logic.constant 17 : i8, 1 : i8 : !byte
      %i1 = arith.constant 17 : i8
      %v2 = simulation.logic.constant 50 : i8, 128 : i8 : !byte
      %i2 = arith.constant 50 : i8
      %v3 = simulation.logic.constant 85 : i8, 85 : i8 : !byte
      %i3 = arith.constant 85 : i8
      %v4 = simulation.logic.constant 170 : i8, 170 : i8 : !byte
      %i4 = arith.constant 170 : i8
      %v5 = simulation.logic.constant 255 : i8, 255 : i8 : !byte
      %i5 = arith.constant 255 : i8
      %v6 = simulation.logic.constant 128 : i8, 64 : i8 : !byte
      %i6 = arith.constant 128 : i8
      %v7 = simulation.logic.constant 1 : i8, 0 : i8 : !byte
      %i7 = arith.constant 1 : i8
      %v8 = simulation.logic.constant 99 : i8, 32 : i8 : !byte
      %i8 = arith.constant 99 : i8
      %v9 = simulation.logic.constant 76 : i8, 0 : i8 : !byte
      %i9 = arith.constant 76 : i8
      %v10 = simulation.logic.constant 201 : i8, 7 : i8 : !byte
      %i10 = arith.constant 201 : i8
      %v11 = simulation.logic.constant 247 : i8, 128 : i8 : !byte
      %i11 = arith.constant 247 : i8
      %v12 = simulation.logic.constant 124 : i8, 32 : i8 : !byte
      %i12 = arith.constant 124 : i8
      %v13 = simulation.logic.constant 42 : i8, 16 : i8 : !byte
      %i13 = arith.constant 42 : i8
      %v14 = simulation.logic.constant 8 : i8, 0 : i8 : !byte
      %i14 = arith.constant 8 : i8
      %v15 = simulation.logic.constant 213 : i8, 3 : i8 : !byte
      %i15 = arith.constant 213 : i8
      %input = simulation.aggregate.construct %v0, %v1, %v2, %v3, %v4, %v5, %v6, %v7, %v8, %v9, %v10, %v11, %v12, %v13, %v14, %v15 : (!byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte, !byte) -> !src
      %iinput = simulation.aggregate.construct %i0, %i1, %i2, %i3, %i4, %i5, %i6, %i7, %i8, %i9, %i10, %i11, %i12, %i13, %i14, %i15 : (i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8, i8) -> !isrc
      simulation.ref.store %input to %src : !src, !simulation.ref<!src>
      simulation.ref.store %iinput to %isrc : !isrc, !simulation.ref<!isrc>
      simulation.call @logic_bytes(%ctx, %src, %dst) : (!simulation.context, !simulation.ref<!src>, !simulation.ref<!dst>) -> ()
      simulation.call @integer_bytes(%ctx, %isrc, %idst, %flags, %out_flags, %triple, %out_triples) : (!simulation.context, !simulation.ref<!isrc>, !simulation.ref<!idst>, !simulation.ref<!simulation.logic<16>>, !simulation.ref<!simulation.logic<16>>, !simulation.ref<!simulation.logic<3>>, !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<3>>>) -> ()
      %out = simulation.ref.load %dst : !simulation.ref<!dst> -> !dst
      %iout = simulation.ref.load %idst : !simulation.ref<!idst> -> !idst
      %flat = simulation.packed.flatten %out : (!dst) -> !simulation.logic<128>
      %iflat = simulation.packed.flatten %iout : (!idst) -> i128
      %expected = simulation.logic.constant 114668772938379240489060399326931410304 : i128, 7795232602645941302638231874961411 : i128 : !simulation.logic<128>
      %iexpected = arith.constant 170626646233012580628439337598257153110 : i128
      %ok = simulation.logic.compare case_eq %flat, %expected : (!simulation.logic<128>, !simulation.logic<128>) -> i1
      %iok = arith.cmpi eq, %iflat, %iexpected : i128
      %actual_flags = simulation.ref.load %out_flags : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %expected_flags = simulation.logic.constant 4369 : i16, 52428 : i16 : !simulation.logic<16>
      %flags_ok = simulation.logic.compare case_eq %actual_flags, %expected_flags : (!simulation.logic<16>, !simulation.logic<16>) -> i1
      %actual_triples = simulation.ref.load %out_triples : !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<3>>> -> !simulation.packed_array<15 : 0 x !simulation.logic<3>>
      %flat_triples = simulation.packed.flatten %actual_triples : (!simulation.packed_array<15 : 0 x !simulation.logic<3>>) -> !simulation.logic<48>
      %expected_triples = simulation.logic.constant 201053554793325 : i48, 80421421917330 : i48 : !simulation.logic<48>
      %triples_ok = simulation.logic.compare case_eq %flat_triples, %expected_triples : (!simulation.logic<48>, !simulation.logic<48>) -> i1
      %fmt = simulation.bytes.constant "byte lanes %0d %0d %0d %0d"
      %channel = arith.constant 1 : i32
      simulation.display %ctx to %channel(%fmt, %ok, %iok, %flags_ok, %triples_ok) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, i1, i1
      simulation.return
    }
    simulation.func @logic_bytes(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !simulation.ref<!src> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
      %dst: !simulation.ref<!dst> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64, schedule.eval.raw_captures} {
      %start = arith.constant 0 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 16 : i32
      %more = arith.cmpi ult, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extui %i : i32 to i65
      %snapshot = simulation.ref.load %src : !simulation.ref<!src> -> !src
      %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!dst>, i65) -> !simulation.ref<!byte>
      %value = simulation.array.extract_dynamic %snapshot[%index] : (!src, i65) -> !byte
      %mask = simulation.logic.constant 85 : i8, 0 : i8 : !byte
      %result = simulation.logic.binary xor %value, %mask : !byte
      simulation.ref.store %result to %out : !byte, !simulation.ref<!byte>
      %one = arith.constant 1 : i32
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
    simulation.func @integer_bytes(
      %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
      %src: !simulation.ref<!isrc> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64},
      %dst: !simulation.ref<!idst> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64},
      %flags: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64},
      %out_flags: !simulation.ref<!simulation.logic<16>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64},
      %triple: !simulation.ref<!simulation.logic<3>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64},
      %out_triples: !simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<3>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64, schedule.eval.raw_captures} {
      %start = arith.constant 15 : i32
      cf.br ^head(%start : i32)
    ^head(%i: i32):
      %limit = arith.constant 0 : i32
      %more = arith.cmpi sge, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %index = arith.extsi %i : i32 to i65
      %snapshot = simulation.ref.load %src : !simulation.ref<!isrc> -> !isrc
      %out = simulation.ref.array_element %dst[%index] : (!simulation.ref<!idst>, i65) -> !simulation.ref<i8>
      %value = simulation.array.extract_dynamic %snapshot[%index] : (!isrc, i65) -> i8
      %mask = arith.constant 85 : i8
      %result = arith.xori %value, %mask : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      // Independent one-bit four-state and byte two-state fields share the
      // iteration group, retaining word widths of 8 and 64 respectively.
      %flag_bits = simulation.ref.load %flags : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %flag_array = simulation.packed.unflatten %flag_bits : (!simulation.logic<16>) -> !simulation.packed_array<15 : 0 x !simulation.logic<1>>
      %out_flag_ref = simulation.ref.dyn_extract %out_flags from %index : (!simulation.ref<!simulation.logic<16>>, i65) -> !simulation.ref<!simulation.logic<1>>
      %flag = simulation.array.extract_dynamic %flag_array[%index] : (!simulation.packed_array<15 : 0 x !simulation.logic<1>>, i65) -> !simulation.logic<1>
      %negated = simulation.logic.unary logical_not %flag : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %negated to %out_flag_ref : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %triple_value = simulation.ref.load %triple : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %triple_ref = simulation.ref.array_element %out_triples[%index] : (!simulation.ref<!simulation.packed_array<15 : 0 x !simulation.logic<3>>>, i65) -> !simulation.ref<!simulation.logic<3>>
      simulation.ref.store %triple_value to %triple_ref : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      %one = arith.constant 1 : i32
      %next = arith.subi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
