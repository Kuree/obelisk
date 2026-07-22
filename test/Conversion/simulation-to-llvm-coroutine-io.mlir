// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @native_io {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.native_io.plain_io.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 initial hierarchy "test.native_io.suspend_io.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.native_io.aggregate_display.9000003"
    obelisk_sim.scope.decl 0

    obelisk_sim.func @plain_io(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %fd: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32,
                    obelisk_sim.hierarchical_name = "top.plain", code_unit_id = 9000001 : i64} {
      %format = obelisk_sim.bytes.constant "%m %l %0d"
      obelisk_sim.display %ctx to %fd(%format, %fd) newline = true radix = 10
          flags = [0, 2, 0]
          {library_cell = "work.plain", scope = "top.plain.named",
           time_multiplier = 1000 : i64}
          : !obelisk_sim.bytes, i32
          loc("native_io.sv":7:9)
      %line, %line_count = obelisk_sim.file.getline %ctx, %fd :
          (!obelisk_sim.context, i32) -> (i13, i32)
      %data, %read_count = obelisk_sim.file.read_packed %ctx, %fd :
          (!obelisk_sim.context, i32) -> (i13, i32)
      obelisk_sim.return
    }

    obelisk_sim.func @suspend_io(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %fd: i32 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32,
                    obelisk_sim.hierarchical_name = "top.suspend", code_unit_id = 9000002 : i64} {
      %format = obelisk_sim.bytes.constant "value=%0d"
      obelisk_sim.display %ctx to %fd(%format, %fd) newline = false radix = 10
          flags = [0, 0] : !obelisk_sim.bytes, i32
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }

    obelisk_sim.func @aggregate_display(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %fd: i32 {obelisk_sim.capture_kind = 2 : i32},
        %value: !obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %format = obelisk_sim.bytes.constant "%0h"
      %flat = obelisk_sim.packed.flatten %value :
          (!obelisk_sim.packed_array<79 : 0 x !obelisk_sim.logic<1>>) ->
          !obelisk_sim.logic<80>
      obelisk_sim.display %ctx to %fd(%format, %flat) newline = false radix = 16
          flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<80>
      obelisk_sim.return
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal constant @{{.*}}("top.plain.named")
// CHECK-DAG: llvm.mlir.global internal constant @{{.*}}("work.plain")
// CHECK-DAG: llvm.func @obelisk_rt_v1_display
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_getline
// CHECK-DAG: llvm.func @obelisk_rt_v1_file_read
// CHECK-DAG: llvm.func @obelisk_rt_v1_buffer_release
// CHECK-LABEL: llvm.func @plain_io(
// CHECK: llvm.call @obelisk_rt_v1_display
// CHECK: llvm.icmp "eq"
// CHECK: llvm.cond_br
// CHECK: llvm.call @obelisk_rt_v1_file_getline
// CHECK: "llvm.intr.memcpy"
// CHECK: llvm.call @obelisk_rt_v1_buffer_release
// CHECK: llvm.call @obelisk_rt_v1_file_read
// CHECK: "llvm.intr.memcpy"
// CHECK: llvm.return {{.*}} : i32
// CHECK-LABEL: llvm.func @plain_io.__obelisk_native_execute
// CHECK: llvm.call @plain_io
// CHECK: llvm.return {{.*}} : i32
// CHECK-LABEL: llvm.func @suspend_io.__obelisk_coro_ramp
// CHECK: llvm.call @obelisk_rt_v1_display
// CHECK: llvm.icmp "eq"
// CHECK: llvm.cond_br
// CHECK: llvm.getelementptr {{.*}}[68]
// CHECK: llvm.store {{.*}} {alignment = 4 : i64} : i32, !llvm.ptr
// CHECK-LABEL: llvm.func @aggregate_display(
// CHECK-SAME: %{{.*}}: !llvm.ptr, %{{.*}}: i32, %{{.*}}: i80, %{{.*}}: i80) -> i32
// CHECK: llvm.call @obelisk_rt_v1_display
// CHECK: llvm.cond_br
// CHECK-NOT: obelisk_sim.packed.
// CHECK-NOT: obelisk_sim.
// CHECK-NOT: obelisk_rt.
// CHECK-NOT: unrealized_conversion_cast
