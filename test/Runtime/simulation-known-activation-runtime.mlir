// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},schedule-version-known-activations{checked-reads=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},schedule-version-known-activations{checked-reads=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   --mlir-print-ir-after=schedule-version-known-activations -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --check-prefix=VERSIONED

// A gated activation copy agrees with the four-state body in every state:
//  - every input known: the copy runs to the end;
//  - the memory element holds X: the copy tests it where it loads and
//    continues in the four-state body, where an OR with known ones and an AND
//    with known zeros still give a known result (IEEE 1800-2023 11.4.8),
//    while the add is all X (11.4.3);
//  - the index holds X: the entry check fails, and reading the element at an
//    X index yields X (7.4.6).
// The counter increment before the tested read runs once per call. Tested
// reads are requested explicitly; the native pipeline keeps the versioned
// body. Without a static state layout the entry check reads the unknown
// planes through the runtime (../Conversion/simulation-known-bits.mlir).
// CHECK: 1 1 1 1

// VERSIONED-LABEL: simulation.func private @step
// VERSIONED-SAME: schedule.native.known_activation
// VERSIONED: schedule.known.bits
// VERSIONED: schedule.native.known_read
// VERSIONED: schedule.known.value

!logic8 = !simulation.logic<8>
!memory = !simulation.unpacked_array<0 : 7 x !simulation.logic<8>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_activation_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 9940000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9940001 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 9940002 in 0 function hierarchy "top.step"
    simulation.storage.decl 0 in 0 : !memory design hierarchy "top.mem"
    simulation.storage.decl 1 in 0 : !logic8 design hierarchy "top.a"
    simulation.storage.decl 2 in 0 : !logic8 design hierarchy "top.count"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9940000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9940001 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %count = simulation.context.storage %ctx[2] : !simulation.ref<!logic8>
      %c2 = arith.constant 2 : i65
      %slot2 = simulation.ref.array_element %mem[%c2] :
          (!simulation.ref<!memory>, i65) -> !simulation.ref<!logic8>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !logic8
      %two = simulation.logic.constant 2 : i8, 0 : i8 : !logic8
      %three = simulation.logic.constant 3 : i8, 0 : i8 : !logic8
      %seven = simulation.logic.constant 7 : i8, 0 : i8 : !logic8
      %unknown = simulation.logic.constant 0 : i8, -1 : i8 : !logic8
      simulation.ref.store %zero to %count : !logic8, !simulation.ref<!logic8>
      simulation.ref.store %seven to %slot2 : !logic8, !simulation.ref<!logic8>
      // mem[3] keeps its initial X.
      simulation.ref.store %two to %a : !logic8, !simulation.ref<!logic8>
      %known = simulation.call @step(%ctx) :
          (!simulation.context) -> !logic8
      simulation.ref.store %three to %a : !logic8, !simulation.ref<!logic8>
      %element = simulation.call @step(%ctx) :
          (!simulation.context) -> !logic8
      simulation.ref.store %unknown to %a : !logic8, !simulation.ref<!logic8>
      %index = simulation.call @step(%ctx) :
          (!simulation.context) -> !logic8
      %calls = simulation.ref.load %count :
          !simulation.ref<!logic8> -> !logic8

      %four = simulation.logic.constant 4 : i8, 0 : i8 : !logic8
      %nine = simulation.logic.constant 9 : i8, 0 : i8 : !logic8
      %ok_known = simulation.logic.compare case_eq %known, %four :
          (!logic8, !logic8) -> i1
      %ok_element = simulation.logic.compare case_eq %element, %nine :
          (!logic8, !logic8) -> i1
      %ok_index = simulation.logic.compare case_eq %index, %unknown :
          (!logic8, !logic8) -> i1
      %ok_calls = simulation.logic.compare case_eq %calls, %three :
          (!logic8, !logic8) -> i1
      %format = simulation.bytes.constant "%0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %ok_known, %ok_element, %ok_index, %ok_calls)
          newline = true radix = <decimal> flags = [0, 0, 0, 0, 0] :
          !simulation.bytes, i1, i1, i1, i1
      simulation.return
    }

    // count += 1; p = a * a; e = mem[a];
    // r = (((e + p) | p) & p) | ((((p | a) & a) | a) & a) ^ a
    // which is (e + p) | p) & p for known a.
    simulation.func private @step(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> !logic8
        attributes {entry_kind = 8 : i32, code_unit_id = 9940002 : i64} {
      %mem = simulation.context.storage %ctx[0] : !simulation.ref<!memory>
      %a = simulation.context.storage %ctx[1] : !simulation.ref<!logic8>
      %count = simulation.context.storage %ctx[2] : !simulation.ref<!logic8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !logic8
      %n = simulation.ref.load %count : !simulation.ref<!logic8> -> !logic8
      %next = simulation.logic.binary add %n, %one : !logic8
      simulation.ref.store %next to %count : !logic8, !simulation.ref<!logic8>
      %x = simulation.ref.load %a : !simulation.ref<!logic8> -> !logic8
      %p = simulation.logic.binary mul %x, %x : !logic8
      %slot = simulation.ref.array_element %mem[%x] :
          (!simulation.ref<!memory>, !logic8) -> !simulation.ref<!logic8>
      %e = simulation.ref.load %slot : !simulation.ref<!logic8> -> !logic8
      %s1 = simulation.logic.binary add %e, %p : !logic8
      %s2 = simulation.logic.binary or %s1, %p : !logic8
      %s3 = simulation.logic.binary and %s2, %p : !logic8
      %q1 = simulation.logic.binary or %p, %x : !logic8
      %q2 = simulation.logic.binary and %q1, %x : !logic8
      %q3 = simulation.logic.binary or %q2, %x : !logic8
      %q4 = simulation.logic.binary and %q3, %x : !logic8
      %q5 = simulation.logic.binary xor %q4, %x : !logic8
      %r = simulation.logic.binary or %s3, %q5 : !logic8
      simulation.return %r : !logic8
    }
  }
}
