// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.feature.class_bitstream_source
} {
  obelisk_sim.design @empty_dispatch {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.class.decl @__obelisk_class_s3_C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.cast"
    obelisk_sim.func @cast(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %object: !obelisk_sim.class_handle<@__obelisk_class_s3_C>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32} {
      %result, %matched, %watch =
          obelisk_sim.recursive.export_bitstream %object {
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 3235077357463657086, 0, 64, 0>
          } : (!obelisk_sim.class_handle<@__obelisk_class_s3_C>) ->
              (i8, i1, !obelisk_sim.managed_watch)
      obelisk_sim.return
    }
  }
}

// The one static dispatch group has zero members and there are no concrete
// schemas.  It remains a valid live site: only null can inhabit the handle.
// CHECK: obelisk.execution.class_bitstream_blob = array<i8: 66, 83, 66, 67
// CHECK: obelisk.feature.class_bitstream
// CHECK: llvm.call @obelisk_rt_v2_recursive_export_bitstream
