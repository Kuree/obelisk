// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | FileCheck %s
// A CFG join preserves one descriptor with a union of possible static lanes.
// Snapshot all of that union, rather than only the width of the read result.
// CHECK: obelisk.execution.sampled_ranges = array<i64: 0, 12>
!word = !simulation.logic<4>
!array = !simulation.unpacked_array<0 : 3 x !word>
!aref = !simulation.ref<!array>
!wref = !simulation.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @joined_sample {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !array design
    simulation.code_unit.decl 1 in 0 function hierarchy "choose"
    simulation.func @choose(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %condition: i1 {simulation.capture_kind = 2 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %array = simulation.context.storage %ctx[0] : !aref
      %first = simulation.ref.subelement %array[[0]] : !aref -> !wref
      %third = simulation.ref.subelement %array[[2]] : !aref -> !wref
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^merge(%first : !wref)
    ^right:
      cf.br ^merge(%third : !wref)
    ^merge(%selected: !wref):
      %value = simulation.assert.sampled_read %ctx from %selected : (!simulation.context, !wref) -> !word
      simulation.return %value : !word
    }
  }
}
