// RUN: talos-opt %s --test-talos-native-state-layout-analysis 2>&1 | FileCheck %s

// IEEE 1800-2023 19.3 constructs each covergroup array element separately, so
// aggregate storage reserves one whole 64-bit runtime handle per element, as it
// does for events.
module {
  simulation.design @handles {
    simulation.covergroup.decl @cg schema 1 debug "cg"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.covergroup_handle<@cg> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 2 x !simulation.covergroup_handle<@cg>> design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_struct<[#simulation.field<name = "flag", type = i1, ordinal = 0, packedOffset = 0>, #simulation.field<name = "group", type = !simulation.covergroup_handle<@cg>, ordinal = 1, packedOffset = 0>]> design
  }
}
// CHECK: native-state bits=
// CHECK-NEXT: bound 1 offset=0 width=64 four-state=false
// CHECK-NEXT: bound 2 offset=64 width=192 four-state=false
// CHECK-NEXT: bound 3 offset=256 width=128 four-state=false
