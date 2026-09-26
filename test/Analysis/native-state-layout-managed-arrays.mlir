// RUN: obelisk-opt %s --test-obelisk-native-state-layout-analysis -o %t 2>&1 | FileCheck %s

// A RAM-sized array has no managed roots. Its trace layout should depend on
// the element type, not require visiting all four million stored words.
!ram = !obelisk_sim.unpacked_array<0 : 4194303 x !obelisk_sim.logic<64>>
!bytes = !obelisk_sim.packed_array<3 : 0 x i8>
!record = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "bytes", type = !bytes, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>
]>
!overlap = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "bits", type = !obelisk_sim.logic<64>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "text", type = !obelisk_sim.string, ordinal = 1, packedOffset = 0>
], isTagged = false>

module {
  obelisk_sim.design @managed_arrays {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !ram design
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.unpacked_array<2 : 0 x !record> design
    obelisk_sim.storage.decl 2 in 0 :
        !obelisk_sim.unpacked_array<-1 : 1 x !overlap> design
    obelisk_sim.storage.decl 3 in 0 :
        !obelisk_sim.unpacked_array<7 : 7 x !obelisk_sim.string> design
  }
}

// Skipping root-free bytes must not hide the following string field. Every
// managed array element remains a root, including overlapping union roots.
// CHECK: native-state bits=268436096
// CHECK-NEXT: bound 1 offset=0 width=268435456 four-state=true{{$}}
// CHECK-NEXT: bound 2 offset=268435456 width=384 four-state=false roots=64, 192, 320{{$}}
// CHECK-NEXT: bound 3 offset=268435840 width=192 four-state=true roots=0, 64, 128 candidate-roots=0:2,64:2,128:2{{$}}
// CHECK-NEXT: bound 4 offset=268436032 width=64 four-state=false roots=0{{$}}
