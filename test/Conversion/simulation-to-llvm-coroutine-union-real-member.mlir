// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// IEEE 1800-2017 7.3 spells its union example `union { int i; shortreal f; }`,
// so a real shares an unpacked union's single piece of storage with the
// integral members; 7.2 and 7.4.2 allow one in an unpacked structure or array
// the same way. That storage lowers to an integer plane, which the real
// crosses as its bit pattern.

!choice = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "bits", type = i8, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "real", type = f64, ordinal = 1, packedOffset = 0>
], isTagged = false>
!reals = !obelisk_sim.unpacked_array<1 : 0 x f64>

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @union_real {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "union_real.exercise"

    obelisk_sim.func @exercise(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: f64 {obelisk_sim.capture_kind = 2 : i32},
        %index: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %default = obelisk_sim.aggregate.default : !choice
      %written = obelisk_sim.aggregate.insert %value into %default[1] :
          (!choice, f64) -> !choice
      %constructed = obelisk_sim.union.construct %value as 1 : (f64) -> !choice
      %member = obelisk_sim.union.extract %constructed[1] : (!choice) -> f64
      %array = obelisk_sim.aggregate.construct %member, %value :
          (f64, f64) -> !reals
      %element = obelisk_sim.aggregate.extract %array[0] : (!reals) -> f64
      %dynamic = obelisk_sim.array.extract_dynamic %array[%index] :
          (!reals, i32) -> f64
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: llvm.func @exercise(
// CHECK-DAG: llvm.bitcast %{{.*}} : f64 to i64
// CHECK-DAG: llvm.bitcast %{{.*}} : i64 to f64
// CHECK-NOT: obelisk_sim.aggregate
// CHECK-NOT: obelisk_sim.union
// CHECK-NOT: obelisk_sim.array.extract_dynamic
