// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %talos --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=PLAN
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4.2: constrained cyclic values precede ordinary draws.
// Mixed fields skip values without a completion, preserve two-value cycles,
// and keep solve-before and ordinary soft preferences conditional on randc.
// Permutation-key draws advance the object stream, including the first draw.
// Joint fields bind a sparse signed enum after a 130-bit ordinary field.
// Failed solves publish neither properties nor tentative cyclic histories;
// restoring the caller RNG isolates history from permitted proposal draws.
// Frozen fields, checker calls, disabled blocks on a fresh instance, and
// temporary explicit selection preserve their required modes and domains.
// All cyclic stages of a class share one compiled relation: each randomize
// plan has one cyclic sampling operation and one ordinary solve (18.4.2).
// PLAN-LABEL: simulation.func private @__talos_randomize_plan___talos_class_s{{[0-9]+}}_Joint_
// PLAN: simulation.random.cycle_sample
// PLAN: simulation.random.solve_wide
// PLAN-NOT: simulation.random.cycle_sample
// PLAN-NOT: simulation.random.solve
// PLAN-LABEL: simulation.func private @__talos_randomize_plan___talos_class_s{{[0-9]+}}_Mixed_
// PLAN: simulation.random.cycle_sample
// PLAN: simulation.random.solve_wide
// PLAN-NOT: simulation.random.cycle_sample
// PLAN-NOT: simulation.random.solve
//
// CHECK: mixed randc cycles 1
// CHECK: mixed randc failure 1
// CHECK: mixed randc modes 1
// CHECK: joint randc cycles 1
// CHECK: joint randc failure and modes 1
// CHECK: joint randc disabled blocks and explicit selection 1

typedef enum bit signed [3:0] {MINUS = -3, ZERO = 0, ONE = 1, FIVE = 5, SEVEN = 7} E;

class Mixed;
  randc bit [2:0] cyc;
  rand bit [3:0] a;
  rand bit [3:0] b;
  int limit = 2;
  constraint hard {
    cyc < limit;
    a == cyc + 4;
    b == a + 1;
    solve a before b;
  }
  constraint preference { soft b == 5; }
endclass

class Joint;
  randc bit [1:0] first;
  rand bit [129:0] payload;
  randc E second;
  bit blocked = 0;
  constraint hard {
    first < 2;
    second == ((first == 0) ? MINUS : SEVEN);
    payload == ((130'h1 << 100) | {124'h0, first, second});
    !blocked;
  }
endclass

module top;
  Mixed m = new;
  Mixed peer = new;
  Joint j = new;
  int valid = 1;
  int result, peer_result, seen, old_first, old_second;
  bit [129:0] old_payload;
  string rng;

  initial begin
    m.srandom(321);
    peer.set_randstate(m.get_randstate());
    rng = m.get_randstate();
    repeat (6) begin
      seen = 0;
      repeat (2) begin
        result = m.randomize();
        if (!result || rng == m.get_randstate()) valid = 0;
        peer_result = peer.randomize();
        if (!result || !peer_result || m.cyc >= 2 || m.a != m.cyc + 4 ||
            m.b != m.a + 1 || m.cyc != peer.cyc || m.a != peer.a || m.b != peer.b)
          valid = 0;
        if ((seen & (1 << m.cyc)) != 0) valid = 0;
        seen |= 1 << m.cyc;
      end
      if (seen != 3) valid = 0;
    end
    $display("mixed randc cycles %0d", valid);

    // A checker call keeps the stream; an infeasible solve keeps properties.
    rng = m.get_randstate();
    result = m.randomize(null);
    if (!result || rng != m.get_randstate()) valid = 0;
    m.limit = 0;
    old_first = m.cyc;
    old_second = m.a;
    result = m.randomize();
    if (result || m.cyc != old_first || m.a != old_second || m.b != m.a + 1) valid = 0;
    m.limit = 2;
    m.set_randstate(rng);
    repeat (8) begin
      result = m.randomize();
      peer_result = peer.randomize();
      if (!result || !peer_result || m.cyc != peer.cyc || m.a != peer.a || m.b != peer.b)
        valid = 0;
    end
    $display("mixed randc failure %0d", valid);

    // A frozen randc field keeps its value and its history.
    m.cyc.rand_mode(0);
    old_first = m.cyc;
    result = m.randomize(a, b);
    if (!result || m.cyc != old_first || m.a != m.cyc + 4 || m.b != m.a + 1) valid = 0;
    rng = m.get_randstate();
    result = m.randomize(null);
    if (!result || rng != m.get_randstate()) valid = 0;
    peer.set_randstate(m.get_randstate());
    m.cyc.rand_mode(1);
    repeat (8) begin
      result = m.randomize();
      peer_result = peer.randomize();
      if (!result || !peer_result || m.cyc != peer.cyc || m.a != peer.a || m.b != peer.b)
        valid = 0;
    end
    $display("mixed randc modes %0d", valid);

    valid = 1;
    repeat (8) begin
      seen = 0;
      repeat (2) begin
        result = j.randomize();
        if (!result || j.first >= 2 || j.second != ((j.first == 0) ? MINUS : SEVEN) ||
            j.payload != ((130'h1 << 100) | {124'h0, j.first, j.second}))
          valid = 0;
        if ((seen & (1 << j.first)) != 0) valid = 0;
        seen |= 1 << j.first;
      end
      if (seen != 3) valid = 0;
    end
    $display("joint randc cycles %0d", valid);

    rng = j.get_randstate();
    result = j.randomize(null);
    if (!result || rng != j.get_randstate()) valid = 0;
    old_first = j.first;
    old_second = j.second;
    old_payload = j.payload;
    j.blocked = 1;
    result = j.randomize();
    if (result || j.first != old_first || int'(j.second) != old_second ||
        j.payload != old_payload)
      valid = 0;
    j.blocked = 0;
    j.first.rand_mode(0);
    result = j.randomize();
    if (!result || j.first != old_first || j.second != ((j.first == 0) ? MINUS : SEVEN) ||
        j.payload != ((130'h1 << 100) | {124'h0, j.first, j.second}))
      valid = 0;
    j.first.rand_mode(1);
    $display("joint randc failure and modes %0d", valid);

    // Without the hard block, second cycles through its whole sparse enum.
    j = new;
    j.hard.constraint_mode(0);
    repeat (4) begin
      seen = 0;
      repeat (5) begin
        result = j.randomize();
        if (!result || !(j.second inside {MINUS, ZERO, ONE, FIVE, SEVEN})) valid = 0;
        if ((seen & (1 << (int'(j.second) + 3))) != 0) valid = 0;
        seen |= 1 << (int'(j.second) + 3);
      end
      if (seen != ((1 << 0) | (1 << 3) | (1 << 4) | (1 << 8) | (1 << 10))) valid = 0;
    end
    j.second.rand_mode(0);
    old_first = j.first;
    old_payload = j.payload;
    repeat (4) begin
      seen = 0;
      repeat (5) begin
        result = j.randomize(second);
        if (!result || j.first != old_first || j.payload != old_payload ||
            !(j.second inside {MINUS, ZERO, ONE, FIVE, SEVEN}))
          valid = 0;
        if ((seen & (1 << (int'(j.second) + 3))) != 0) valid = 0;
        seen |= 1 << (int'(j.second) + 3);
      end
      if (seen != ((1 << 0) | (1 << 3) | (1 << 4) | (1 << 8) | (1 << 10))) valid = 0;
    end
    $display("joint randc disabled blocks and explicit selection %0d", valid);
    $finish();
  end
endmodule
