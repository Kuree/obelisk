// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode | FileCheck %s
// A CFG join preserves one descriptor with a union of possible static lanes.
// Snapshot all of that union, rather than only the width of the read result.
// CHECK: obelisk.execution.sampled_ranges = array<i64: 0, 12>
!word = !obelisk_sim.logic<4>
!array = !obelisk_sim.unpacked_array<0 : 3 x !word>
!aref = !obelisk_sim.ref<!array>
!wref = !obelisk_sim.ref<!word>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @joined_sample {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !array design
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "choose"
    obelisk_sim.func @choose(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %condition: i1 {obelisk_sim.capture_kind = 2 : i32}) -> !word attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %array = obelisk_sim.context.storage %ctx[0] : !aref
      %first = obelisk_sim.ref.subelement %array[[0]] : !aref -> !wref
      %third = obelisk_sim.ref.subelement %array[[2]] : !aref -> !wref
      cf.cond_br %condition, ^left, ^right
    ^left:
      cf.br ^merge(%first : !wref)
    ^right:
      cf.br ^merge(%third : !wref)
    ^merge(%selected: !wref):
      %value = obelisk_sim.assert.sampled_read %ctx from %selected : (!obelisk_sim.context, !wref) -> !word
      obelisk_sim.return %value : !word
    }
  }
}
