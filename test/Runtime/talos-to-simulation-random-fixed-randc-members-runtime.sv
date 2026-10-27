// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4 and 18.4.2: qualified fixed array leaves of a structure
// member retain individual enum and integral cycles, including shared static
// histories and singular modes. Disabled enum leaves may retain unnamed
// encodings.
// PLAN-DAG: simulation.class.field {{.*}} : !simulation.unpacked_array<0 : 1 x !simulation.string>
// PLAN-DAG: $rand_member_0_domain
// PLAN: simulation.random.cycle_sample
// PLAN-SAME: SAMP

// Each 3-valued enum leaf and each 4-valued integral leaf visits its whole
// domain before repeating; non-random members are untouched.
// RUNTIME: fixed randc qualified enum and integral member cycles 1
// A static structure member has one history shared by every instance.
// RUNTIME: fixed randc static shared member cycles 1
// A copied object keeps the history of a disabled enum leaf, which may hold an
// encoding outside the enumerators.
// RUNTIME: fixed randc member frozen enum history 1
// Singular rand_mode on a static leaf is visible through every instance and
// keeps its history while disabled.
// RUNTIME: fixed randc static member element modes 1
typedef enum int { NEG = -5, POS = 7, HIGH = 2147483647 } Kind;
`define W(c) ((c) == NEG ? 1 : (c) == POS ? 2 : 3)
`define BIT(c) ((c) == NEG ? 1 : (c) == POS ? 2 : (c) == HIGH ? 4 : 0)

class Members;
  typedef struct {
    randc Kind codes [0:1];
    randc bit [1:0] numbers [2:1];
    rand bit [7:0] ordinary;
    int untouched;
  } Fields;
  rand Fields local_values;
  static rand Fields shared_values;
  constraint relation {
    local_values.ordinary == `W(local_values.codes[0])
        + `W(local_values.codes[1]) + local_values.numbers[2]
        + local_values.numbers[1];
    shared_values.ordinary == `W(shared_values.codes[0])
        + `W(shared_values.codes[1]) + shared_values.numbers[2]
        + shared_values.numbers[1];
  }
endclass

module top;
  Members a, b, d;
  int enum_seen [0:1];
  int number_seen [2:1];
  int frozen_code;
  bit ok;

  initial begin
    a = new;
    b = new;
    a.srandom(13);
    b.srandom(29);
    a.local_values.untouched = 99;
    a.shared_values.untouched = 101;
    ok = 1;
    for (int step = 0; step < 12; ++step) begin
      if (step % 3 == 0) foreach (enum_seen[i]) enum_seen[i] = 0;
      if (step % 4 == 0) foreach (number_seen[i]) number_seen[i] = 0;
      if (!a.randomize()) ok = 0;
      foreach (enum_seen[i]) begin
        enum_seen[i] |= `BIT(a.local_values.codes[i]);
        if (step % 3 == 2 && enum_seen[i] != 7) ok = 0;
      end
      foreach (number_seen[i]) begin
        number_seen[i] |= 1 << a.local_values.numbers[i];
        if (step % 4 == 3 && number_seen[i] != 15) ok = 0;
      end
      if (a.local_values.untouched != 99 || a.shared_values.untouched != 101)
        ok = 0;
    end
    $display("fixed randc qualified enum and integral member cycles %0d", ok);

    ok = 1;
    for (int step = 0; step < 12; ++step) begin
      if (step % 3 == 0) foreach (enum_seen[i]) enum_seen[i] = 0;
      if (step % 4 == 0) foreach (number_seen[i]) number_seen[i] = 0;
      if (step % 2 == 0) begin
        if (!a.randomize()) ok = 0;
      end else begin
        if (!b.randomize()) ok = 0;
      end
      foreach (enum_seen[i]) begin
        if (a.shared_values.codes[i] !== b.shared_values.codes[i]) ok = 0;
        enum_seen[i] |= `BIT(a.shared_values.codes[i]);
        if (step % 3 == 2 && enum_seen[i] != 7) ok = 0;
      end
      foreach (number_seen[i]) begin
        if (a.shared_values.numbers[i] !== b.shared_values.numbers[i]) ok = 0;
        number_seen[i] |= 1 << a.shared_values.numbers[i];
        if (step % 4 == 3 && number_seen[i] != 15) ok = 0;
      end
    end
    $display("fixed randc static shared member cycles %0d", ok);

    ok = 1;
    d = new a;
    a.local_values.codes[0].rand_mode(0);
    a.local_values.codes[0] = Kind'(0);
    if (a.local_values.codes[0].rand_mode() != 0 ||
        a.local_values.codes[1].rand_mode() != 1) ok = 0;
    repeat (4)
      if (!a.randomize() || a.local_values.codes[0] !== 0) ok = 0;
    a.local_values.codes[0].rand_mode(1);
    a.local_values.codes[1].rand_mode(0);
    d.local_values.codes[1].rand_mode(0);
    a.local_values.numbers.rand_mode(0);
    d.local_values.numbers.rand_mode(0);
    a.shared_values.rand_mode(0);
    d.set_randstate(a.get_randstate());
    repeat (3) begin
      if (!a.randomize() || !d.randomize()) ok = 0;
      if (a.local_values.codes[0] !== d.local_values.codes[0] ||
          a.get_randstate() != d.get_randstate()) ok = 0;
    end
    $display("fixed randc member frozen enum history %0d", ok);

    ok = 1;
    a.shared_values.rand_mode(1);
    frozen_code = a.shared_values.codes[0];
    a.shared_values.codes[0].rand_mode(0);
    if (b.shared_values.codes[0].rand_mode() != 0 ||
        b.shared_values.codes[1].rand_mode() != 1) ok = 0;
    repeat (4)
      if (!b.randomize() || a.shared_values.codes[0] !== frozen_code) ok = 0;
    b.shared_values.codes[0].rand_mode(1);
    enum_seen[0] = 0;
    repeat (3) begin
      if (!a.randomize()) ok = 0;
      enum_seen[0] |= `BIT(a.shared_values.codes[0]);
    end
    if (enum_seen[0] != 7 || a.shared_values.codes[0].rand_mode() != 1) ok = 0;
    $display("fixed randc static member element modes %0d", ok);
  end
endmodule
