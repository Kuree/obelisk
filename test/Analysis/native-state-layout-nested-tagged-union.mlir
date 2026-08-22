// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis 2>&1 | FileCheck %s

// IEEE 1800-2017 7.3.2 gives a tagged union a tag alongside the member value,
// and the native layout stores that tag above the member payload. A tagged
// union nested inside another one therefore occupies payload *and* tag inside
// its parent: sizing the member by its payload alone would leave the inner tag
// sharing bits with whatever the parent stores next -- for these two types,
// with the parent's own tag.

!jmp = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "JmpU", type = i10, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "JmpC", type = i12, ordinal = 1, packedOffset = 0>
], isTagged = true>
!instr = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "Add", type = i15, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "Jmp", type = !jmp, ordinal = 1, packedOffset = 0>
], isTagged = true>

module {
  obelisk_sim.design @nested_tagged_union {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !jmp design
    obelisk_sim.storage.decl 1 in 0 : !instr design
  }
}

// The inner union is 10 + 12 payload bits plus a 2-bit tag, and the outer one
// holds that whole 24-bit member after its own 15-bit member, plus its own
// 2-bit tag.
// CHECK: bound 1 offset=0 width=24 four-state=false
// CHECK-NEXT: bound 2 offset={{[0-9]+}} width=41 four-state=false
