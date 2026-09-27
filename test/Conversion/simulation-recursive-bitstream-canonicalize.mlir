// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module attributes {obelisk.feature.class_bitstream_source} {
  func.func @null_source() -> (i24, i1, !simulation.managed_watch) {
    %null = simulation.managed.null :
        !simulation.dynamic_array<!simulation.dynamic_array<i8>>
    %result, %matched, %watch =
        simulation.recursive.export_bitstream %null {
          plan = array<i64: 5407724112, 3, 64, 0,
              8589934595, 0, 1, 0, 64, 0,
              4294967299, 0, 1, 0, 8, 0,
              1, 0, 8, 0, 0, 8>
        } : (!simulation.dynamic_array<!simulation.dynamic_array<i8>>) ->
            (i24, i1, !simulation.managed_watch)
    return %result, %matched, %watch : i24, i1, !simulation.managed_watch
  }

  func.func @null_class() -> (i8, i1, !simulation.managed_watch) {
    %null = simulation.class.null :
        !simulation.class_handle<@__obelisk_class_s3_C>
    %result, %matched, %watch =
        simulation.recursive.export_bitstream %null {
          class_site_id = 1 : i64,
          plan = array<i64: 9702691408, 1, 64, 0,
              5, 0, 3235077357463657086, 0, 64, 0>
        } : (!simulation.class_handle<@__obelisk_class_s3_C>) ->
            (i8, i1, !simulation.managed_watch)
    return %result, %matched, %watch : i8, i1, !simulation.managed_watch
  }
}

// CHECK-LABEL: func.func @null_source
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i24
// CHECK: %[[FALSE:.*]] = arith.constant false
// CHECK: %[[WATCH:.*]] = simulation.managed.watch.null
// CHECK-NOT: recursive.export_bitstream
// CHECK: return %[[ZERO]], %[[FALSE]], %[[WATCH]]

// CHECK-LABEL: func.func @null_class
// CHECK: %[[CLASS_ZERO:.*]] = arith.constant 0 : i8
// CHECK: %[[CLASS_FALSE:.*]] = arith.constant false
// CHECK: %[[CLASS_WATCH:.*]] = simulation.managed.watch.null
// CHECK-NOT: recursive.export_bitstream
// CHECK: return %[[CLASS_ZERO]], %[[CLASS_FALSE]], %[[CLASS_WATCH]]
