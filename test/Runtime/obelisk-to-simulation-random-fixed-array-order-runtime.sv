// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4 and 18.5.9: fixed unpacked arrays randomize each
// integral element and a selected element may be a solve-before operand. The
// array field is planned as one integral bitstream with per-element masks.
// PLAN: simulation.class.field {{.*}}debug_name = "values"
// PLAN: simulation.random.solve_wide {{.*}}SAMP

// values[0] -> values[1] == 0 has 2^32 + 1 assignments; ordering values[0]
// before values[1] must still pick its two values with equal probability
// (128 draws stay within 33..95 ones). Enumerated elements keep their named
// domains (all eight combinations appear), an ascending 129-bit aggregate
// follows its foreach constraint, and a failed inline constraint preserves
// every array while the proposal draws advance the object's RNG state.
// RUNTIME: array solving 1 1 1
// RUNTIME-NEXT: array failure 1 1 1 1 1

typedef enum bit [1:0] {A = 0, B = 3} array_enum;
typedef bit [42:0] wide_t;
typedef bit [63:0] vals_t;
typedef bit [5:0] enums_bits_t;
typedef bit [128:0] wide_bits_t;
typedef bit [63:0] mask_t;

class C;
  rand bit [31:0] values [1:0];
  rand array_enum enums [2:0];
  rand wide_t wide [-2:0];
  constraint constraints {
    values[0] <= 1;
    values[0] != 0 -> values[1] == 0;
    solve values[0] before values[1];
    foreach (wide[wi]) wide[wi] == wi + 7;
  }
endclass

module top;
  C object = new;
  bit valid = 1;
  int unsigned ones = 0;
  mask_t seen = 0;
  int status;
  vals_t saved_values;
  bit [5:0] saved_enums;
  wide_bits_t saved_wide;
  string saved_state;
  int unsigned conflict = 2;
  initial begin
    repeat (128) begin
      valid &= object.randomize() == 1;
      valid &= object.values[0] <= 1;
      valid &= object.values[0] == 0 || object.values[1] == 0;
      valid &= object.enums[0] == A || object.enums[0] == B;
      valid &= object.enums[1] == A || object.enums[1] == B;
      valid &= object.enums[2] == A || object.enums[2] == B;
      valid &= wide_bits_t'(object.wide) == {43'd5, 43'd6, 43'd7};
      ones += object.values[0];
      seen |= mask_t'(1) << enums_bits_t'(object.enums);
    end
    $display("array solving %0d %0d %0d", valid, ones > 32 && ones < 96,
             seen == 64'h9009_0000_0000_9009);
    saved_values = vals_t'(object.values);
    saved_enums = enums_bits_t'(object.enums);
    saved_wide = wide_bits_t'(object.wide);
    saved_state = object.get_randstate();
    status = object.randomize() with { values[0] == conflict; };
    $display("array failure %0d %0d %0d %0d %0d", status == 0,
             saved_values == vals_t'(object.values),
             saved_enums == enums_bits_t'(object.enums),
             saved_wide == wide_bits_t'(object.wide),
             saved_state != object.get_randstate());
  end
endmodule
