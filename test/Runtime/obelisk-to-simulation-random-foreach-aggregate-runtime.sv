// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9 and 18.5.7.1: complete finite semantic
// ranks retain uniform solutions under dynamic state foreach predicates.
// PLAN: ^bb{{[0-9]+}}(%[[RANK:[0-9]+]]: i64, %[[COUNT:[0-9]+]]: i64, %{{[0-9]+}}: i262, %{{[0-9]+}}: i64):
// PLAN: arith.remui %[[RANK]], {{.*}} : i64
// PLAN: arith.addi %[[COUNT]], {{.*}} : i64
// PLAN: arith.addi %[[RANK]], {{.*}} : i64

// A sparse 130-bit enum array under a dynamic excluded-value foreach keeps
// every enumerator legal for the unconstrained leaf and the implication tag.
// RUNTIME: foreach wide aggregate semantic ranks 1
// A disabled leaf holding an unnamed encoding is frozen while the remaining
// leaf is still drawn from its restricted domain.
// RUNTIME: foreach aggregate frozen leaf 1
typedef enum logic signed [129:0] {
  NEG = -130'sd5,
  SMALL = 130'sd7,
  HIGH = 130'sh10_0000_0000_0000_0000_0000_0003
} Kind;

class C;
  rand Kind values [1:0];
  rand bit [1:0] tag;
  Kind excluded [];
  constraint data {
    foreach (excluded[i]) values[1] != excluded[i];
  }
  constraint relation {
    if (values[0] == NEG) tag == 0; else tag == 1;
  }
endclass

module top;
  C c;
  bit good;
  initial begin
    good = 1;
    c = new;
    c.srandom(17);
    c.excluded = new[1];
    c.excluded[0] = HIGH;
    for (int i = 0; i < 24; i++) begin
      good &= c.randomize() && (c.values[1] == NEG || c.values[1] == SMALL);
      good &= c.values[0] inside {NEG, SMALL, HIGH};
      good &= c.tag == (c.values[0] == NEG ? 0 : 1);
    end
    $display("foreach wide aggregate semantic ranks %0d", good);

    good = 1;
    c.values[0] = Kind'(0);
    c.values[0].rand_mode(0);
    for (int i = 0; i < 24; i++)
      good &= c.randomize() && c.values[0] === 0 && c.tag == 1 &&
              (c.values[1] == NEG || c.values[1] == SMALL);
    good &= c.values[0].rand_mode() == 0 && c.values[1].rand_mode() == 1;
    $display("foreach aggregate frozen leaf %0d", good);
    $finish();
  end
endmodule
