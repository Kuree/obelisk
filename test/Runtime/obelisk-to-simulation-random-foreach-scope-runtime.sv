// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9 and 18.5.7.1: complete finite semantic
// ranks retain uniform solutions under dynamic state foreach predicates.
// PLAN: ^bb{{[0-9]+}}(%[[RANK:[0-9]+]]: i64, %[[COUNT:[0-9]+]]: i64, %{{[0-9]+}}: i64, %{{[0-9]+}}: i64):
// PLAN: arith.remui %[[RANK]], {{.*}} : i64
// PLAN: arith.addi %[[COUNT]], {{.*}} : i64
// PLAN: arith.addi %[[RANK]], {{.*}} : i64

// The foreach excludes twelve of sixteen values; the four remaining values
// must each be drawn roughly uniformly (64 of 256 draws, bounded to 36..99).
// RUNTIME: foreach scope uniform gapped domain 1
module top;
  bit [3:0] value;
  int excluded [];
  int counts [0:3];
  int slot;
  bit good;
  initial begin
    good = 1;
    excluded = '{2, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    for (int i = 0; i < 256; i++) begin
      good &= std::randomize(value) with {
        foreach (excluded[j]) value != excluded[j];
      };
      slot = value == 0 ? 0 : value == 1 ? 1 : value == 3 ? 2 : 3;
      good &= value inside {0, 1, 3, 7};
      counts[slot]++;
    end
    for (int i = 0; i < 4; i++) good &= counts[i] > 35 && counts[i] < 100;
    $display("foreach scope uniform gapped domain %0d", good);
    $finish();
  end
endmodule
