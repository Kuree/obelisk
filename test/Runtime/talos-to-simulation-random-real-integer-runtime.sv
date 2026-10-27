// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// RUN: %talos --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=PLAN
// REQUIRES: compiled-sampler

// IEEE 1800-2023 6.12.1 and 20.5: active numeric-coordinate casts, frozen
// nonfinite conversions, integer-to-real candidates and IEEE round trips.
// Casts round halfway away from zero; $rtoi truncates. Compiled plans agree
// with that at both precisions, including 8/32/130-bit results, nonfinite
// four-state conversion planes, X/Z consumption, modes and failed checks.
// CHECK: real integer conversions valid 1
//
// Every cast, truncation and classification relation is compiled to a sampler
// plan: each solve operation carries a SAMP program (2 narrow, 6 wide).
// PLAN-COUNT-8: simulation.random.solve{{(_wide)?}} {{.*}}SAMP

typedef bit [129:0] Wide;
typedef bit [7:0] Byte;

class Casts;
  rand real x;
  rand shortreal y;
  real point = 1.5;
  int rounded = 2;
  int truncated = 1;
  Wide wideValue = 2;
  Byte byteValue = 2;
  constraint domain { x == point; real'(y) == point; }
  constraint rounding { int'(x) == rounded; int'(y) == rounded; }
  constraint truncation { $rtoi(x) == int'(truncated); $rtoi(real'(y)) == int'(truncated); }
  constraint widths {
    Wide'(x) == wideValue;
    Wide'(y) == wideValue;
    Byte'(x) == byteValue;
    Byte'(y) == byteValue;
  }
endclass

// A nonfinite real converts to an unknown integer in every cast form.
class UnknownCasts;
  rand real x;
  rand bit [2:0] tag;
  constraint c {
    $isunknown($rtoi(x));
    $isunknown(integer'(x));
    tag == 5;
  }
endclass

class IntegralReal;
  rand logic [7:0] value;
  rand bit [2:0] tag;
  real expected = 134;
  constraint c {
    $itor(value) == expected;
    real'(value) == expected;
    tag == 2;
  }
endclass

class BitRoundTrips;
  rand bit [63:0] x;
  rand bit [31:0] y;
  bit [63:0] expected64;
  bit [31:0] expected32;
  constraint c {
    x == expected64;
    y == expected32;
    $realtobits($bitstoreal(x)) == x;
    $shortrealtobits($bitstoshortreal(y)) == y;
  }
endclass

module top;
  Casts c = new;
  UnknownCasts u = new;
  IntegralReal i = new;
  BitRoundTrips b = new;
  bit valid = 1;
  string saved;
  initial begin
    c.rand_mode(0);
    c.x.rand_mode(1);
    c.y.rand_mode(1);
    for (int k = 0; k < 8; k++) begin
      case (k)
        0: begin c.point = 1.5; c.rounded = 2; c.truncated = 1; end
        1: begin c.point = -1.5; c.rounded = -2; c.truncated = -1; end
        2: begin c.point = 2.5; c.rounded = 3; c.truncated = 2; end
        3: begin c.point = -2.5; c.rounded = -3; c.truncated = -2; end
        4: begin c.point = 1.25; c.rounded = 1; c.truncated = 1; end
        5: begin c.point = -1.25; c.rounded = -1; c.truncated = -1; end
        6: begin c.point = 0; c.rounded = 0; c.truncated = 0; end
        7: begin c.point = 3.6893488147419103e+19; c.rounded = 0; c.truncated = 0; end
      endcase
      c.x = c.point;
      c.y = shortreal'(c.point);
      c.wideValue = (k == 7) ? (Wide'(1) << 65) : Wide'(c.rounded);
      c.byteValue = Byte'(c.rounded);
      valid &= c.randomize() && c.x == c.point && real'(c.y) == c.point;
      valid &= int'(c.x) == c.rounded && $rtoi(c.x) == int'(c.truncated);
      valid &= Wide'(c.x) == c.wideValue && Wide'(c.y) == c.wideValue;
      valid &= Byte'(c.x) == c.byteValue && Byte'(c.y) == c.byteValue;
    end

    // An unsatisfiable check fails without consuming the random stream.
    c.point = 1.5;
    c.x = 1.5;
    c.y = shortreal'(1.5);
    c.rounded = 2;
    c.truncated = 7;
    c.wideValue = Wide'(2);
    c.byteValue = Byte'(2);
    saved = c.get_randstate();
    valid &= !c.randomize() && c.get_randstate() == saved;
    c.truncation.constraint_mode(0);
    valid &= c.randomize() && c.x == 1.5 && real'(c.y) == 1.5;
    c.truncation.constraint_mode(1);

    // A NaN payload in a frozen real is preserved by a failed solve.
    c.truncated = 1;
    valid &= c.randomize(null);
    c.x = $bitstoreal(64'h7ff8000000000042);
    saved = c.get_randstate();
    valid &= !c.randomize(null) && c.get_randstate() == saved;
    valid &= $realtobits(c.x) == 64'h7ff8000000000042;

    // Nonfinite frozen sources satisfy the $isunknown consumers.
    u.x.rand_mode(0);
    u.x = $bitstoreal(64'h7ff8000000000042);
    valid &= u.randomize() && u.tag == 5;
    valid &= $realtobits(u.x) == 64'h7ff8000000000042;
    u.x = $bitstoreal(64'hfff0000000000000);
    valid &= u.randomize() && u.tag == 5;

    // X/Z bits of a frozen integral source are consumed unchanged.
    i.value.rand_mode(0);
    i.value = 8'b1x0z0110;
    valid &= i.randomize() && i.tag == 2 && i.value === 8'b1x0z0110;
    i.value.rand_mode(1);
    valid &= i.randomize() && i.value == 8'd134 && i.tag == 2;

    for (int k = 0; k < 5; k++) begin
      case (k)
        0: begin b.expected64 = 64'h3ff8000000000000; b.expected32 = 32'd1069547520; end
        1: begin b.expected64 = 64'h8000000000000000; b.expected32 = 32'd2147483648; end
        2: begin b.expected64 = 64'h7ff8000000000042; b.expected32 = 32'd2143289410; end
        3: begin b.expected64 = 64'hfff0000000000000; b.expected32 = 32'd4286578688; end
        4: begin b.expected64 = 64'h1; b.expected32 = 32'd1; end
      endcase
      valid &= b.randomize() && b.x == b.expected64 && b.y == b.expected32;
    end
    $display("real integer conversions valid %0d", valid);
  end
endmodule
