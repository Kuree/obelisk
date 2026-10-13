// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-mark-bounded-loops,obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=BOUNDS < %t.llvm.mlir

// LRM 7.4.5 and 11.5.1: proven loop-body indices allow direct accesses,
// including nested unpacked/packed selects and dynamic slices. The payload
// still has independent X/Z planes. The exit index and a bounded inner loop
// beneath an invalid/unknown outer selection must retain their fallbacks.
// Bounds proofs remove handle-overflow checks on the body read and both wide
// and narrow writes. At loop exit, retain the array index guard and invalid
// read value, without duplicating clipped-slice/handle-overflow checks.
// BOUNDS-LABEL: llvm.func @copy(
// BOUNDS-NOT: 2147483647
// BOUNDS: llvm.load {{.*}} -> i8
// BOUNDS-NOT: 2147483647
// BOUNDS: llvm.store {{.*}} : i8, !llvm.ptr
// BOUNDS-NOT: 2147483647
// BOUNDS: llvm.store {{.*}} : i64, !llvm.ptr
// BOUNDS-NOT: 2147483647
// BOUNDS: llvm.cond_br
// BOUNDS-NOT: 2147483647
// BOUNDS: llvm.load {{.*}} -> i64
// BOUNDS: llvm.return
// BOUNDS-LABEL: llvm.func @invalid_parent(
// BOUNDS: 2147483647
!word = !simulation.packed_array<63 : 0 x !simulation.logic<1>>
!ports = !simulation.unpacked_array<-1 : 0 x !word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", schedule.native_scheduler = 3 : i32} {
  simulation.design @bounded_access {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<128> design
    simulation.storage.decl 1 in 0 : !ports design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "copy"
    simulation.code_unit.decl 3 in 0 function hierarchy "invalid_parent"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<128>>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!ports>
      %narrow = simulation.context.storage %ctx[2] : !simulation.ref<!word>
      %payload = simulation.logic.constant 1512366075204170929049582354406559215 : i128, 319014720176802247504873742109612441615 : i128 : !simulation.logic<128>
      simulation.ref.store %payload to %src : !simulation.logic<128>, !simulation.ref<!simulation.logic<128>>
      %exit = simulation.call @copy(%ctx) : (!simulation.context) -> !word
      %after = simulation.ref.load %dst : !simulation.ref<!ports> -> !ports
      %lo = simulation.aggregate.extract %after[0] : (!ports) -> !word
      %hi = simulation.aggregate.extract %after[1] : (!ports) -> !word
      %lobits = simulation.packed.flatten %lo : (!word) -> !simulation.logic<64>
      %hibits = simulation.packed.flatten %hi : (!word) -> !simulation.logic<64>
      %joined = simulation.logic.concat %hibits, %lobits : (!simulation.logic<64>, !simulation.logic<64>) -> !simulation.logic<128>
      %copied = simulation.logic.compare case_eq %joined, %payload : (!simulation.logic<128>, !simulation.logic<128>) -> i1
      %small = simulation.ref.load %narrow : !simulation.ref<!word> -> !word
      %smallbits = simulation.packed.flatten %small : (!word) -> !simulation.logic<64>
      %smallok = simulation.logic.compare case_eq %smallbits, %hibits : (!simulation.logic<64>, !simulation.logic<64>) -> i1
      %bad = simulation.logic.constant 1 : i65, 0 : i65 : !simulation.logic<65>
      %negative = simulation.logic.constant -2 : i65, 0 : i65 : !simulation.logic<65>
      %x = simulation.logic.constant 0 : i65, 1 : i65 : !simulation.logic<65>
      %huge = simulation.logic.constant 18446744073709551616 : i65, 0 : i65 : !simulation.logic<65>
      simulation.call @invalid_parent(%ctx, %bad) : (!simulation.context, !simulation.logic<65>) -> ()
      simulation.call @invalid_parent(%ctx, %negative) : (!simulation.context, !simulation.logic<65>) -> ()
      simulation.call @invalid_parent(%ctx, %x) : (!simulation.context, !simulation.logic<65>) -> ()
      simulation.call @invalid_parent(%ctx, %huge) : (!simulation.context, !simulation.logic<65>) -> ()
      %final = simulation.ref.load %dst : !simulation.ref<!ports> -> !ports
      %finallo = simulation.aggregate.extract %final[0] : (!ports) -> !word
      %finalhi = simulation.aggregate.extract %final[1] : (!ports) -> !word
      %finallobits = simulation.packed.flatten %finallo : (!word) -> !simulation.logic<64>
      %finalhibits = simulation.packed.flatten %finalhi : (!word) -> !simulation.logic<64>
      %unchangedlo = simulation.logic.compare case_eq %finallobits, %lobits : (!simulation.logic<64>, !simulation.logic<64>) -> i1
      %unchangedhi = simulation.logic.compare case_eq %finalhibits, %hibits : (!simulation.logic<64>, !simulation.logic<64>) -> i1
      %unchanged = arith.andi %unchangedlo, %unchangedhi : i1
      %exitbits = simulation.packed.flatten %exit : (!word) -> !simulation.logic<64>
      %format = simulation.bytes.constant "copied=%b narrow=%b exit=%h unchanged=%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %copied, %smallok, %exitbits, %unchanged) newline = true radix = <decimal> flags = [0, 0, 0, 0, 0] : !simulation.bytes, i1, i1, !simulation.logic<64>, i1
      simulation.return
    }
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %src = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<128>>
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!ports>
      %narrow = simulation.context.storage %ctx[2] : !simulation.ref<!word>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %first = arith.constant -1 : i32
      %count = arith.constant 64 : i32
      cf.br ^outer(%first : i32)
    ^outer(%p: i32):
      %more = arith.cmpi slt, %p, %one : i32
      cf.cond_br %more, ^start, ^exit
    ^start:
      cf.br ^inner(%zero : i32)
    ^inner(%b: i32):
      %bits = arith.cmpi slt, %b, %count : i32
      cf.cond_br %bits, ^body, ^next
    ^body:
      %port = simulation.ref.array_element %dst[%p] : (!simulation.ref<!ports>, i32) -> !simulation.ref<!word>
      %bit = simulation.ref.array_element %port[%b] : (!simulation.ref<!word>, i32) -> !simulation.ref<!simulation.logic<1>>
      %ordinal = arith.addi %p, %one : i32
      %base = arith.muli %ordinal, %count : i32
      %offset = arith.addi %base, %b : i32
      %source = simulation.ref.dyn_extract %src from %offset : (!simulation.ref<!simulation.logic<128>>, i32) -> !simulation.ref<!simulation.logic<1>>
      %value = simulation.ref.load %source : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.ref.store %value to %bit : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %small = simulation.ref.array_element %narrow[%b] : (!simulation.ref<!word>, i32) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %value to %small : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %nextbit = arith.addi %b, %one : i32
      cf.br ^inner(%nextbit : i32)
    ^next:
      %nextport = arith.addi %p, %one : i32
      cf.br ^outer(%nextport : i32)
    ^exit:
      %outside = simulation.ref.array_element %dst[%p] : (!simulation.ref<!ports>, i32) -> !simulation.ref<!word>
      %ones = simulation.logic.constant -1 : i64, 0 : i64 : !simulation.logic<64>
      %word = simulation.packed.unflatten %ones : (!simulation.logic<64>) -> !word
      simulation.ref.store %word to %outside : !word, !simulation.ref<!word>
      %result = simulation.ref.load %outside : !simulation.ref<!word> -> !word
      simulation.return %result : !word
    }
    simulation.func @invalid_parent(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %p: !simulation.logic<65> {simulation.capture_kind = 2 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %dst = simulation.context.storage %ctx[1] : !simulation.ref<!ports>
      %port = simulation.ref.array_element %dst[%p] : (!simulation.ref<!ports>, !simulation.logic<65>) -> !simulation.ref<!word>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %count = arith.constant 64 : i32
      %value = simulation.logic.constant true, false : !simulation.logic<1>
      cf.br ^loop(%zero : i32)
    ^loop(%b: i32):
      %more = arith.cmpi slt, %b, %count : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %bit = simulation.ref.array_element %port[%b] : (!simulation.ref<!word>, i32) -> !simulation.ref<!simulation.logic<1>>
      simulation.ref.store %value to %bit : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %next = arith.addi %b, %one : i32
      cf.br ^loop(%next : i32)
    ^exit:
      simulation.return
    }
  }
}
