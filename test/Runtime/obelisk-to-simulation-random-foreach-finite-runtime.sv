// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9 and 18.5.7.1: live state-array and queue
// foreach predicates select uniformly from a complete finite semantic domain.
// A 130-bit signed enum has three values, independently of its representation
// width. Unequal companion completion counts retain the joint distribution.
// PLAN: ^bb{{[0-9]+}}(%[[RANK:[0-9]+]]: i64, %[[COUNT:[0-9]+]]: i64, %{{[0-9]+}}: i132, %{{[0-9]+}}: i64):
// PLAN: arith.remui %[[RANK]], {{.*}} : i64
// PLAN: arith.addi %[[COUNT]], {{.*}} : i64
// PLAN: arith.addi %[[RANK]], {{.*}} : i64

// The joint (value, tag) solutions are NEG/0 and SMALL/0..2, each drawn about
// 64 of 256 times (bounded to 36..99) although SMALL has three completions.
// RUNTIME: foreach wide finite joint sampling 1
// Queue elements exclude values; an empty array excludes nothing.
// RUNTIME: foreach queue and empty array 1
// An exhausted domain fails without touching the object; disabled constraints,
// frozen values and checker mode still hold the remaining constraints.
// RUNTIME: foreach modes checker and failure 1
typedef enum logic signed [129:0] {
  NEG = -130'sd5,
  SMALL = 130'sd7,
  HIGH = 130'sh10_0000_0000_0000_0000_0000_0003
} Kind;

class C;
  rand Kind value;
  rand bit [1:0] tag;
  Kind excluded [];
  Kind queued [$];
  constraint arrays { foreach (excluded[i]) value != excluded[i]; }
  constraint queues { foreach (queued[i]) value != queued[i]; }
  constraint relation {
    if (value == NEG) tag == 0;
    else if (value == SMALL) tag inside {[0:2]};
    else tag inside {[1:2]};
  }
endclass

module top;
  C c;
  int counts [0:3];
  int slot;
  bit good;
  Kind saved;
  bit [1:0] old_tag;
  initial begin
    good = 1;
    c = new;
    c.srandom(13);
    c.excluded = new[1];
    c.excluded[0] = HIGH;
    for (int i = 0; i < 256; i++) begin
      good &= c.randomize();
      slot = c.value == NEG ? 0 : c.tag + 1;
      good &= slot >= 0 && slot < 4 && (c.value == NEG || c.value == SMALL);
      counts[slot]++;
    end
    for (int i = 0; i < 4; i++) good &= counts[i] > 35 && counts[i] < 100;
    $display("foreach wide finite joint sampling %0d", good);

    good = 1;
    c.excluded = new[0];
    c.queued.push_back(NEG);
    c.queued.push_back(SMALL);
    for (int i = 0; i < 12; i++)
      good &= c.randomize() && c.value == HIGH && (c.tag == 1 || c.tag == 2);
    $display("foreach queue and empty array %0d", good);

    good = 1;
    c.queued.push_back(HIGH);
    saved = c.value;
    old_tag = c.tag;
    good &= !c.randomize() && c.value === saved && c.tag == old_tag;
    c.queues.constraint_mode(0);
    for (int i = 0; i < 12; i++) good &= c.randomize();
    c.value = Kind'(0);
    c.value.rand_mode(0);
    c.tag = 1;
    for (int i = 0; i < 12; i++)
      good &= c.randomize() && c.value === 0 && (c.tag == 1 || c.tag == 2);
    c.tag.rand_mode(0);
    old_tag = c.tag;
    good &= c.randomize(null) && c.tag == old_tag && c.value === 0;
    $display("foreach modes checker and failure %0d", good);
    $finish();
  end
endmodule
