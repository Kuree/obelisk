// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   --mlir-print-ir-after=schedule-version-known-regions -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=VERSIONED

// Two-state copies switch back to the four-state code exactly where an input
// holds X/Z, so native results match the canonical bytecode bit for bit:
//  - `first` starts X and becomes known; mem[3] keeps its initial X in the
//    middle of the sweep (two-state -> four-state -> two-state);
//  - an add with an X operand is all X (IEEE 1800-2023 11.4.3);
//  - an OR with a known 1 absorbs an X bit, a known 0 does not (11.4.8);
//  - straight-line code with an X live-in, and with known inputs.
// CHECK: 1 1 1 1 1

// VERSIONED: schedule.known.value
// VERSIONED: schedule.native.known_read

!logic8 = !simulation.logic<8>
!memory = !simulation.unpacked_array<0 : 7 x !simulation.logic<8>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_versioning_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9930000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9930001 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 9930002 in 0 function hierarchy "top.compute"
    simulation.storage.decl 0 in 0 : !memory design hierarchy "top.mem"
    simulation.storage.decl 1 in 0 : !logic8 design hierarchy "top.unset"
    simulation.storage.decl 2 in 0 : !logic8 design hierarchy "top.p"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9930000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9930001 : i64} {
      %ok:5 = simulation.call @compute(%ctx) :
          (!simulation.context) -> (i1, i1, i1, i1, i1)
      %format = simulation.bytes.constant "%0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %ok#0, %ok#1, %ok#2, %ok#3, %ok#4)
          newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] :
          !simulation.bytes, i1, i1, i1, i1, i1
      simulation.return
    }

    simulation.func private @compute(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (i1, i1, i1, i1, i1)
        attributes {entry_kind = 8 : i32, code_unit_id = 9930002 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c3 = arith.constant 3 : i32
      %c8 = arith.constant 8 : i32
      cf.br ^fill(%c0 : i32)
    // mem[i] = 3 * i, except mem[3], which keeps its initial X.
    ^fill(%fi: i32):
      %fmore = arith.cmpi slt, %fi, %c8 : i32
      cf.cond_br %fmore, ^fill_body, ^filled
    ^fill_body:
      %skip = arith.cmpi eq, %fi, %c3 : i32
      cf.cond_br %skip, ^fill_next, ^fill_store
    ^fill_store:
      %fwide = arith.extsi %fi : i32 to i65
      %fslot = simulation.ref.array_element %mem[%fwide] :
          (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %triple = arith.muli %fi, %c3 : i32
      %tbits = arith.trunci %triple : i32 to i8
      %tvalue = simulation.logic.from_bits %tbits : i8 -> !logic8
      simulation.ref.store %tvalue to %fslot : !logic8, !simulation.ref<!logic8>
      cf.br ^fill_next
    ^fill_next:
      %fnext = arith.addi %fi, %c1 : i32
      cf.br ^fill(%fnext : i32)
    ^filled:
      %unknown8 = simulation.logic.constant 0 : i8, -1 : i8 : !logic8
      %zero8 = simulation.logic.constant 0 : i8, 0 : i8 : !logic8
      cf.br ^sweep(%c0, %unknown8, %zero8, %zero8 :
          i32, !logic8, !logic8, !logic8)

    // first = last i with mem[i][1] set; sum += mem[i]; any |= mem[i].
    ^sweep(%i: i32, %first: !logic8, %sum: !logic8, %any: !logic8):
      %more = arith.cmpi slt, %i, %c8 : i32
      cf.cond_br %more, ^sweep_body, ^swept(%first, %sum, %any :
          !logic8, !logic8, !logic8)
    ^sweep_body:
      %wide = arith.extsi %i : i32 to i65
      %slot = simulation.ref.array_element %mem[%wide] :
          (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %element = simulation.ref.load %slot :
          !simulation.ref<!logic8> -> !logic8
      %next_sum = simulation.logic.binary add %sum, %element : !logic8
      %next_any = simulation.logic.binary or %any, %element : !logic8
      %bit = simulation.logic.extract %element from 1 :
          !logic8 -> !simulation.logic<1>
      %taken = simulation.logic.is_true %bit : !simulation.logic<1>
      cf.cond_br %taken, ^sweep_set, ^sweep_next(%first : !logic8)
    ^sweep_set:
      %ibits = arith.trunci %i : i32 to i8
      %ivalue = simulation.logic.from_bits %ibits : i8 -> !logic8
      cf.br ^sweep_next(%ivalue : !logic8)
    ^sweep_next(%kept: !logic8):
      %inext = arith.addi %i, %c1 : i32
      cf.br ^sweep(%inext, %kept, %next_sum, %next_any :
          i32, !logic8, !logic8, !logic8)

    ^swept(%first_out: !logic8, %sum_out: !logic8, %any_out: !logic8):
      // Straight-line code reading an X variable and a known variable.
      %unset = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %p = simulation.context.storage %ctx[2] : !simulation.ref<!logic8>
      %five = simulation.logic.constant 5 : i8, 0 : i8 : !logic8
      simulation.ref.store %five to %p : !logic8, !simulation.ref<!logic8>
      %u = simulation.ref.load %unset : !simulation.ref<!logic8> -> !logic8
      %pv = simulation.ref.load %p : !simulation.ref<!logic8> -> !logic8
      %u_sum = simulation.logic.binary add %u, %pv : !logic8
      %u_mix = simulation.logic.binary and %u_sum, %pv : !logic8
      %p_sum = simulation.logic.binary add %pv, %pv : !logic8
      %p_mix = simulation.logic.binary or %p_sum, %pv : !logic8

      %want_first = simulation.logic.constant 6 : i8, 0 : i8 : !logic8
      %want_sum = simulation.logic.constant 0 : i8, -1 : i8 : !logic8
      %want_any = simulation.logic.constant 31 : i8, -32 : i8 : !logic8
      %want_u = simulation.logic.constant 0 : i8, 5 : i8 : !logic8
      %want_p = simulation.logic.constant 15 : i8, 0 : i8 : !logic8
      %ok_first = simulation.logic.compare case_eq %first_out, %want_first :
          (!logic8, !logic8) -> i1
      %ok_sum = simulation.logic.compare case_eq %sum_out, %want_sum :
          (!logic8, !logic8) -> i1
      %ok_any = simulation.logic.compare case_eq %any_out, %want_any :
          (!logic8, !logic8) -> i1
      %ok_u = simulation.logic.compare case_eq %u_mix, %want_u :
          (!logic8, !logic8) -> i1
      %ok_p = simulation.logic.compare case_eq %p_mix, %want_p :
          (!logic8, !logic8) -> i1
      simulation.return %ok_first, %ok_sum, %ok_any, %ok_u, %ok_p :
          i1, i1, i1, i1, i1
    }
  }
}
