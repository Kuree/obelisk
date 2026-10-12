// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3, 18.8 and 20.9: a live $countones target is compared
// with one shared prefix sum instead of enumerating every possible count. The
// SAMP header of the live CountPlan solve (65 random bits 'A', three captures)
// is bound by its node count: the low byte, then a high byte below 0x10 means
// fewer than 4096 nodes, so an enumeration of all 66 cardinalities cannot
// return unnoticed.
// PLAN: simulation.random.solve{{.*}}SAMP\01\00\00\00{{(\\[0-9A-Fa-f]{2}|[^"\\])}}\0{{[0-9A-Fa-f]}}\00\00\01\00\00\00A\00\00\00\03\00\00\00

// Equality and inequality cover every count of the 65-bit input and the
// signed targets -1 and 66 that no input satisfies, frozen inputs, a joint
// random count (JointPlan), a three-bit target that truncates the sum
// (TruncatedPlan), and a 130-bit target whose high bits must not alias a legal
// count (WidePlan, 20.9 limits counts to 32 bits).
// RUNTIME: linear count plans valid 1

class CountPlan;
  rand bit [64:0] value;
  int wanted;
  bit exclude;
  constraint relation {
    if (exclude) $countones(value) != wanted;
    else $countones(value) == wanted;
  }
endclass

class JointPlan;
  rand bit [7:0] value;
  rand int count;
  constraint relation { count == $countones(value); }
endclass

class TruncatedPlan;
  rand bit [64:0] value;
  bit [2:0] wanted;
  constraint relation { 3'($countones(value)) == wanted; }
endclass

class WidePlan;
  rand bit [12:0] value;
  bit [129:0] wanted;
  constraint relation { $countones(value) == wanted; }
endclass

module top;
  CountPlan c = new;
  JointPlan j = new;
  TruncatedPlan t = new;
  WidePlan w = new;
  bit good = 1;
  bit [64:0] previous;
  int status;
  initial begin
    for (int k = -1; k <= 66; k++) begin
      c.wanted = k;
      c.exclude = 0;
      previous = c.value;
      status = c.randomize();
      good &= status == (k >= 0 && k <= 65);
      if (status) good &= $countones(c.value) == k;
      else good &= c.value == previous;
      c.exclude = 1;
      status = c.randomize();
      good &= status && $countones(c.value) != k;
    end
    c.exclude = 0;
    c.value = '1;
    c.value.rand_mode(0);
    c.wanted = 64;
    good &= !c.randomize() && c.value == '1;
    c.wanted = 65;
    good &= c.randomize() && c.value == '1;
    c.value.rand_mode(1);
    for (int k = 0; k < 32; k++) begin
      good &= j.randomize() && j.count == $countones(j.value);
      t.wanted = k[2:0];
      good &= t.randomize() && 3'($countones(t.value)) == t.wanted;
      w.wanted = k % 14;
      good &= w.randomize() && $countones(w.value) == w.wanted;
    end
    w.wanted = 130'h1 << 100;
    good &= !w.randomize();
    w.wanted = '1;
    good &= !w.randomize();
    w.wanted = 13;
    good &= w.randomize() && w.value == '1;
    $display("linear count plans valid %0d", good);
    if (!good) $fatal(1, "linear count plan violation");
    $finish;
  end
endmodule
