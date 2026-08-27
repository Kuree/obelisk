// RUN: obelisk-opt %s --canonicalize | FileCheck %s

module {
  func.func @null_source() -> (i24, i1, !obelisk_sim.managed_watch) {
    %null = obelisk_sim.managed.null :
        !obelisk_sim.dynamic_array<!obelisk_sim.dynamic_array<i8>>
    %result, %matched, %watch =
        obelisk_sim.recursive.export_bitstream %null {
          plan = array<i64: 5407724112, 3, 64, 0,
              8589934595, 0, 1, 0, 64, 0,
              4294967299, 0, 1, 0, 8, 0,
              1, 0, 8, 0, 0, 8>
        } : (!obelisk_sim.dynamic_array<!obelisk_sim.dynamic_array<i8>>) ->
            (i24, i1, !obelisk_sim.managed_watch)
    return %result, %matched, %watch : i24, i1, !obelisk_sim.managed_watch
  }
}

// CHECK-LABEL: func.func @null_source
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i24
// CHECK: %[[FALSE:.*]] = arith.constant false
// CHECK: %[[WATCH:.*]] = obelisk_sim.managed.watch.null
// CHECK-NOT: recursive.export_bitstream
// CHECK: return %[[ZERO]], %[[FALSE]], %[[WATCH]]
