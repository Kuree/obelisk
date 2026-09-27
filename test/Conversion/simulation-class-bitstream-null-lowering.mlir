// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.feature.class_bitstream_source
} {
  simulation.design @null_class_bitstream {
    simulation.scope.decl 0 hierarchy "top"
    simulation.class.decl @__obelisk_class_s3_C id 1 {
      is_abstract = true, is_final = false, is_interface = false
    }
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.cast"
    simulation.func @cast(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %null = simulation.class.null :
          !simulation.class_handle<@__obelisk_class_s3_C>
      %result, %matched, %watch =
          simulation.recursive.export_bitstream %null {
            plan = array<i64: 9702691408, 1, 64, 0,
                5, 0, 3235077357463657086, 0, 64, 0>
          } : (!simulation.class_handle<@__obelisk_class_s3_C>) ->
              (i8, i1, !simulation.managed_watch)
      simulation.return
    }
  }
}

// CHECK-NOT: obelisk.execution.class_bitstream_blob
// CHECK-NOT: obelisk_rt_v1_class_bitstream
// CHECK-NOT: obelisk_rt_v2_recursive_export_bitstream
