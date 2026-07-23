// RUN: obelisk -O0 -emit-sim %s | FileCheck %s --check-prefix=O0
// RUN: obelisk -O1 -emit-sim --compile-threads=1 %s > %t.o1.single
// RUN: obelisk -O1 -emit-sim --compile-threads=4 %s > %t.o1.threaded
// RUN: diff -u %t.o1.single %t.o1.threaded
// RUN: FileCheck %s --check-prefix=OPT < %t.o1.single
// RUN: obelisk -O2 -emit-sim %s | FileCheck %s --check-prefix=OPT
// RUN: obelisk -O3 -emit-sim %s | FileCheck %s --check-prefix=OPT

module dead_boundaries;
  bit seed;
  bit [1:0] copy;

  // Recursion makes this deliberately ineligible for inlining. The top-level
  // call discards the primary function value but observes the output copy.
  function automatic bit recursive_copy(
      input bit value,
      output bit [1:0] copied);
    bit [1:0] nested_copy;
    copied = value;
    if (value == 0)
      recursive_copy = 0;
    else
      recursive_copy = recursive_copy(!value, nested_copy);
  endfunction

  initial begin
    seed = 1;
    recursive_copy(seed, copy);
  end
endmodule

// O0: obelisk_sim.func private @unit_0
// O0-SAME: -> (i1, !obelisk_sim.packed_array<1 : 0 x i1>)
// O0: %{{.*}}:2 = obelisk_sim.call @unit_0
// O0-SAME: -> (i1, !obelisk_sim.packed_array<1 : 0 x i1>)

// OPT: obelisk_sim.func private @unit_0
// OPT-SAME: -> !obelisk_sim.packed_array<1 : 0 x i1>
// OPT: %{{.*}} = obelisk_sim.call @unit_0
// OPT-SAME: -> !obelisk_sim.packed_array<1 : 0 x i1>
// OPT-NOT: -> (i1, !obelisk_sim.packed_array<1 : 0 x i1>)
