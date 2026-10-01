// RUN: obelisk-opt %s --verify-roundtrip -o %t
// RUN: FileCheck %s --implicit-check-not='"slang.' --implicit-check-not='"obelisk.' --implicit-check-not='"runtime.' --implicit-check-not='"schedule.' --implicit-check-not='"obelisk_sdf.' < %t
// RUN: obelisk-opt %t -o %t.again
// RUN: diff %t %t.again

// Symbols use MLIR's @name spelling, including quoted names when necessary.
// CHECK: slang.symbol.root @slang_root attributes {node_id = 0 : i64}
// CHECK: slang.type.string_type @"string with spaces" attributes {node_id = 1 : i64, semantic_type = !slang.string}
// CHECK: obelisk.sv.symbol.root @obelisk_root attributes {node_id = 0 : i64}
// CHECK: obelisk.sv.type.string_type @"string with spaces" attributes {node_id = 1 : i64, semantic_type = !obelisk.string}
module {
  slang.symbol.root @slang_root attributes {node_id = 0 : i64} {
    slang.type.string_type @"string with spaces" attributes {
      node_id = 1 : i64, semantic_type = !slang.string
    } {}
  }
  obelisk.sv.symbol.root @obelisk_root attributes {node_id = 0 : i64} {
    obelisk.sv.type.string_type @"string with spaces" attributes {
      node_id = 1 : i64, semantic_type = !obelisk.string
    } {}
  }

  // Materializers infer their fixed ABI types and accept empty argument lists.
  // CHECK-LABEL: func.func @materializers
  // CHECK: runtime.argument.empty
  // CHECK: runtime.argument.array(%{{.*}})
  // CHECK: runtime.argument.array()
  // CHECK: runtime.bytes.size %{{.*}} : !runtime.bytes
  // CHECK: runtime.status.to_bits %{{.*}}
  // CHECK: runtime.status.from_bits %{{.*}}
  // CHECK: runtime.file_descriptor.from_bits %{{.*}}
  // CHECK: runtime.file_descriptor.to_bits %{{.*}}
  func.func @materializers(%status: !runtime.status, %bits: i32) {
    %empty = runtime.argument.empty
    %args = runtime.argument.array(%empty)
    %no_args = runtime.argument.array()
    %bytes = runtime.bytes.constant "abc"
    %size = runtime.bytes.size %bytes : !runtime.bytes
    %status_bits = runtime.status.to_bits %status
    %roundtrip_status = runtime.status.from_bits %status_bits
    %fd = runtime.file_descriptor.from_bits %bits
    %fd_bits = runtime.file_descriptor.to_bits %fd
    return
  }

  // Variable pointer address spaces remain explicit; fixed result types do not.
  // CHECK-LABEL: func.func @native
  // CHECK: schedule.scratch 16
  // CHECK: schedule.bytes.address "bytes" = "abc" alignment 4 : !llvm.ptr<3>
  // CHECK: schedule.gc.root_range.push %{{.*}} slots %{{.*}} count %{{.*}} : !llvm.ptr<2>, !llvm.ptr<3>
  // CHECK: schedule.gc.root_range.pop %{{.*}} : !llvm.ptr<2>
  // CHECK: schedule.observer(%{{.*}} : i64) captures 1
  // CHECK: schedule.observer() captures 0
  func.func @native(%record: !llvm.ptr<2>, %slots: !llvm.ptr<3>, %count: i64) {
    %scratch = schedule.scratch 16
    %address = schedule.bytes.address "bytes" = "abc" alignment 4 : !llvm.ptr<3>
    %status = schedule.gc.root_range.push %record slots %slots count %count
      : !llvm.ptr<2>, !llvm.ptr<3>
    schedule.gc.root_range.pop %record : !llvm.ptr<2>
    %observer = schedule.observer(%count : i64) captures 1 {
      schedule.native.observer_id = 0 : i64,
      schedule.native.observer_width = 1 : i32,
      schedule.native.observer_four_state = false,
      schedule.native.dependency_kinds = array<i32>,
      schedule.native.dependency_widths = array<i32>,
      schedule.native.dependency_capture_indices = array<i32>
    }
    %constant_observer = schedule.observer() captures 0 {
      schedule.native.observer_id = 1 : i64,
      schedule.native.observer_width = 1 : i32,
      schedule.native.observer_four_state = false,
      schedule.native.dependency_kinds = array<i32>,
      schedule.native.dependency_widths = array<i32>,
      schedule.native.dependency_capture_indices = array<i32>
    }
    return
  }

  // CHECK-LABEL: func.func @simulation_types
  // CHECK: simulation.string.literal "hello"
  // CHECK: simulation.string.length %{{.*}} : (!simulation.string) -> i64
  func.func @simulation_types() {
    %string = simulation.string.literal "hello"
    %length = simulation.string.length %string : (!simulation.string) -> i64
    return
  }
}
