// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
// RUN: FileCheck %s --check-prefix=PLAN --implicit-check-not=': !llvm.ptr -> i65536' < %t.native.mlir

// Runtime behavior is checked in ../Runtime/simulation-wide-word-runtime.test.

// Check full vector equality, two distinct low limbs, and the far end of both
// planes. Force native -O0 execution so constant folding cannot hide codegen
// errors; compare against the true bytecode tier and literal expected values.
// PLAN: llvm.xor {{.*}} : vector<1024xi64>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @wide_words {
    simulation.scope.decl 0 hierarchy "wide_words"
    simulation.storage.decl 0 in 0 : !simulation.logic<65536> design hierarchy "wide_words.a"
    simulation.storage.decl 1 in 0 : !simulation.logic<65536> design hierarchy "wide_words.b"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "wide_words.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "wide_words.test"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<65536>>
      %b = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<65536>>
      %p = simulation.spawn @test(%ctx, %a, %b) : !simulation.context, !simulation.ref<!simulation.logic<65536>>, !simulation.ref<!simulation.logic<65536>> -> !simulation.process
      simulation.return
    }
    simulation.func @test(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.logic<65536>> {simulation.capture_kind = 1 : i32},
        %b: !simulation.ref<!simulation.logic<65536>> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %pattern = simulation.logic.constant 36893488147419103233 : i65536, 0 : i65536 : !simulation.logic<65536>
      %zero = simulation.logic.constant 0 : i65536, 0 : i65536 : !simulation.logic<65536>
      %ones = simulation.logic.constant -1 : i65536, 0 : i65536 : !simulation.logic<65536>
      %allx = simulation.logic.constant 0 : i65536, -1 : i65536 : !simulation.logic<65536>
      %allz = simulation.logic.constant -1 : i65536, -1 : i65536 : !simulation.logic<65536>
      simulation.ref.store %pattern to %a : !simulation.logic<65536>, !simulation.ref<!simulation.logic<65536>>
      %loaded = simulation.ref.load %a : !simulation.ref<!simulation.logic<65536>> -> !simulation.logic<65536>
      %inverse = simulation.logic.binary xnor %loaded, %zero : !simulation.logic<65536>
      simulation.ref.store %inverse to %b : !simulation.logic<65536>, !simulation.ref<!simulation.logic<65536>>
      %read = simulation.ref.load %b : !simulation.ref<!simulation.logic<65536>> -> !simulation.logic<65536>
      %expected = simulation.logic.constant -36893488147419103234 : i65536, 0 : i65536 : !simulation.logic<65536>
      %ok = simulation.logic.compare case_eq %read, %expected : (!simulation.logic<65536>, !simulation.logic<65536>) -> i1
      %low = simulation.ref.extract %b from 0 : !simulation.ref<!simulation.logic<65536>> -> !simulation.ref<!simulation.logic<64>>
      %second = simulation.ref.extract %b from 64 : !simulation.ref<!simulation.logic<65536>> -> !simulation.ref<!simulation.logic<64>>
      %last = simulation.ref.extract %b from 65472 : !simulation.ref<!simulation.logic<65536>> -> !simulation.ref<!simulation.logic<64>>
      %vlow = simulation.ref.load %low : !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %vsecond = simulation.ref.load %second : !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %vlast = simulation.ref.load %last : !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %limbs = simulation.bytes.constant "limbs %h %h %h"
      %fd = arith.constant 1 : i32
      simulation.display %ctx to %fd(%limbs, %vlow, %vsecond, %vlast) newline = true radix = <decimal> flags = [0, 0, 0, 0] : !simulation.bytes, !simulation.logic<64>, !simulation.logic<64>, !simulation.logic<64>
      simulation.ref.store %allz to %a : !simulation.logic<65536>, !simulation.ref<!simulation.logic<65536>>
      %z = simulation.ref.load %a : !simulation.ref<!simulation.logic<65536>> -> !simulation.logic<65536>
      %x = simulation.logic.binary xor %z, %ones : !simulation.logic<65536>
      simulation.ref.store %x to %b : !simulation.logic<65536>, !simulation.ref<!simulation.logic<65536>>
      %readx = simulation.ref.load %b : !simulation.ref<!simulation.logic<65536>> -> !simulation.logic<65536>
      %okx = simulation.logic.compare case_eq %readx, %allx : (!simulation.logic<65536>, !simulation.logic<65536>) -> i1
      %different = simulation.logic.compare case_ne %readx, %ones : (!simulation.logic<65536>, !simulation.logic<65536>) -> i1
      %notEqual = simulation.logic.compare case_eq %loaded, %zero : (!simulation.logic<65536>, !simulation.logic<65536>) -> i1
      %unequal = simulation.logic.compare case_ne %loaded, %zero : (!simulation.logic<65536>, !simulation.logic<65536>) -> i1
      %text = simulation.bytes.constant "words %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%text, %ok, %okx, %different, %notEqual, %unequal) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, i1, i1, i1
      simulation.return
    }
  }
}
