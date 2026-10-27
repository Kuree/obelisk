// RUN: talos-opt %s | talos-opt | FileCheck %s

// CHECK: module attributes {
// CHECK-SAME: native.debug_timing
// CHECK-SAME: native.frame_checkpoint = #native.frame_checkpoint<layout = "e-m:e", functions = [@d::@process]>
// CHECK-SAME: native.optimization_level = 3 : i32
// CHECK-SAME: native.partition_manifests = [#native.design_partitions<design = "d", partitions = [#native.partition<id = "primary", owners = ["primary"], members = [@process], imports = [], exports = [], dependencies = []>]>]
// CHECK-SAME: native.physical_partition_manifest = [#native.partition<id = "primary", members = [@process], imports = [@leaf], exports = [], dependencies = ["unit:1"]>]
module attributes {
  native.debug_timing, native.optimization_level = 3 : i32,
  native.frame_checkpoint = #native.frame_checkpoint<layout = "e-m:e",
                                                      functions = [@d::@process]>,
  native.partition_manifests = [
    #native.design_partitions<design = "d", partitions = [
      #native.partition<id = "primary", owners = ["primary"],
                        members = [@process], imports = [], exports = [],
                        dependencies = []>]>],
  native.physical_partition_manifest = [
    #native.partition<id = "primary", members = [@process], imports = [@leaf],
                      exports = [], dependencies = ["unit:1"]>]} {
  // CHECK-LABEL: func.func private @process
  // CHECK-SAME: native.frame = #native.frame<size = 16, alignment = 8, checksum = 7, continuations = [0, 1], fields = [#native.frame_field<kind = 1, flags = 0, offset = 0, size = 8, alignment = 8, reserved = 0>]>
  // CHECK-SAME: native.frame_inputs = #native.frame_inputs<signature = (i32) -> (), identity = [1, 2, 0], abi = [16, 8, 7, 0], continuations = [#native.frame_site<identity = [1, 1, 0, 8], types = [i32]>]>
  // CHECK-SAME: native.partition = "primary"
  // CHECK-SAME: native.two_state_block_unknowns = [array<i64: 0>, array<i64>]
  func.func private @process(i32) attributes {
    native.partition = "primary",
    native.frame = #native.frame<size = 16, alignment = 8, checksum = 7,
        continuations = [0, 1],
        fields = [#native.frame_field<kind = 1, flags = 0, offset = 0, size = 8,
                                      alignment = 8, reserved = 0>]>,
    native.frame_inputs = #native.frame_inputs<signature = (i32) -> (),
        identity = [1, 2, 0], abi = [16, 8, 7, 0],
        continuations = [#native.frame_site<identity = [1, 1, 0, 8],
                                            types = [i32]>]>,
    native.two_state_block_unknowns = [array<i64: 0>, array<i64>]}
}
