// RUN: %talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: z3

// IEEE 1800-2023 18.5.4: unique compares all unpacked leaf elements, including
// multidimensional arrays, fixed state arrays and slices. The operands are
// expanded to leaf elements before Z3 planning, so each randomize call lowers
// to one structural sampler (SAMP section) instead of a runtime checker.
// PLAN-COUNT-3: simulation.random.solve {{.*}}SAMP

// Class constraints contain an ascending negative-index matrix slice and a
// one-element array that rand_mode(0) turns into state, whose live value
// changes the legal leaf set (the frontend rejects never-random operands). A
// singular uniqueness operand contributes no restriction. Duplicate references
// retain their identity and make the solve fail without writing any random
// property. Scope tests mix an array with a singular variable and restrict a
// slice whose unselected endpoints intentionally have equal values.
// RUNTIME: array uniqueness 1 1
// RUNTIME-NEXT: duplicate leaf 1 1 1
// RUNTIME-NEXT: scope uniqueness 1
// RUNTIME-NEXT: slice uniqueness 1 1 1

typedef bit [2:0] w3_t;
typedef bit [11:0] w12_t;

class C;
  rand bit [2:0] multi [-1:0][2:1];
  rand bit [2:0] blocked [1];
  rand bit [2:0] single;
  constraint constraints {
    unique {multi[-1:0], blocked};
    foreach (multi[ui, uj]) multi[ui][uj] < 5;
    unique {single};
  }
endclass

module top;
  C object = new;
  bit valid = 1;
  int unsigned seen = 0;
  w12_t saved;
  w3_t saved_single;
  int status;
  bit scope_valid = 1;
  bit [2:0] array [3];
  bit [2:0] ban;
  bit [2:0] slices [4:1];
  initial begin
    object.blocked.rand_mode(0);
    repeat (64) begin
      valid = valid && object.randomize() == 1 &&
              ((1 << object.multi[-1][2]) | (1 << object.multi[-1][1]) |
               (1 << object.multi[0][2]) | (1 << object.multi[0][1])) == 30;
      seen |= 1 << object.single;
    end
    object.blocked[0] = 4;
    repeat (64) begin
      valid = valid && object.randomize() == 1 &&
              ((1 << object.multi[-1][2]) | (1 << object.multi[-1][1]) |
               (1 << object.multi[0][2]) | (1 << object.multi[0][1])) == 15;
      seen |= 1 << object.single;
    end
    $display("array uniqueness %0d %0d", valid, seen == 255);

    saved = w12_t'(object.multi);
    saved_single = object.single;
    status = object.randomize() with { unique {multi, multi[-1][2]}; };
    $display("duplicate leaf %0d %0d %0d", status == 0,
             saved == w12_t'(object.multi), saved_single == object.single);

    repeat (32)
      scope_valid = scope_valid &&
          std::randomize(array, ban) with {
            ban == 3;
            unique {array, ban};
            array[0] < 4; array[1] < 4; array[2] < 4;
          } == 1 &&
          ((1 << array[0]) | (1 << array[1]) | (1 << array[2])) == 7;
    $display("scope uniqueness %0d", scope_valid);

    status = std::randomize(slices) with {
      unique {slices[3:2]};
      slices[4] == 0; slices[1] == 0; slices[3] < 2; slices[2] < 2;
    };
    $display("slice uniqueness %0d %0d %0d", status, slices[3] + slices[2] == 1,
             slices[4] == slices[1]);
  end
endmodule
