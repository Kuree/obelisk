// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4.1: real and shortreal random properties and scope
// variables sample from numeric-coordinate plans compiled into the program.
// Live bounds enter as rank conversions (binary64 ranks are i2100, binary32
// ranks i279, a mixed class i2379) and every solve carries a SAMP bundle.
// PLAN-DAG: simulation.random.real_to_rank {{.*}}: f64 -> i2100
// PLAN-DAG: simulation.random.real_to_rank {{.*}}: f32 -> i279
// PLAN-DAG: simulation.random.real_from_rank {{.*}}: i2100 -> f64
// PLAN-DAG: simulation.random.real_from_rank {{.*}}: i279 -> f32
// PLAN-DAG: simulation.random.solve_wide {{.*}}SAMP{{.*}}-> (i2379, i1, i64)
// PLAN-DAG: simulation.random.solve_wide {{.*}}SAMP{{.*}}-> (i2100, i1, i64)

// The run checks, with a fixed seed, that every draw satisfies its strict
// bounds, that the soft preferences hold when they are enabled, that the
// (0,1) and (1,2) halves of (0,2) are about equally likely (18.4.1), that a
// shortreal bound pair around 1.0 has exactly one legal shortreal and its
// promoted-to-real midpoint has none, and that failed solves, NaN bounds and
// checker-only calls leave the properties and the RNG state untouched. Frozen
// NaN payloads are kept, while an explicit randomize(x) list overrides
// rand_mode (18.11).
// RUNTIME: real source valid 1 density 1

class RealPacket;
  rand real x;
  rand shortreal y;
  real lower = 0.0;
  real upper = 2.0;
  constraint bounds { lower < x; x < upper; lower < y; y < upper; }
  constraint preferences { soft x < 1.0; soft y < 1.0; }
endclass

module top;
  RealPacket p;
  real sx;
  shortreal sy;
  real sl = -2.0;
  real su = -1.0;
  bit valid = 1;
  bit density;
  int low_x = 0;
  int low_y = 0;
  int status;
  real old_x;
  shortreal old_y;
  string old_state;
  initial begin
    p = new;
    p.preferences.constraint_mode(0);
    repeat (128) begin
      status = p.randomize();
      valid &= status && p.x > 0.0 && p.x < 2.0 && p.y > 0.0 && p.y < 2.0;
      if (p.x < 1.0) low_x++;
      if (p.y < 1.0) low_y++;
    end
    density = low_x >= 44 && low_x <= 84 && low_y >= 44 && low_y <= 84;
    p.x = 0.5;
    p.x.rand_mode(0);
    valid &= p.randomize() && p.x == 0.5 && p.y > 0.0 && p.y < 2.0;
    p.x.rand_mode(1);
    p.preferences.constraint_mode(1);
    valid &= p.randomize() && p.x > 0.0 && p.x < 1.0 && p.y > 0.0 && p.y < 1.0;
    p.preferences.constraint_mode(0);
    p.lower = 1.0000000596046448;
    p.upper = 1.0000001192092896;
    old_x = p.x;
    old_y = p.y;
    old_state = p.get_randstate();
    valid &= !p.randomize() && p.x == old_x && p.y == old_y && p.get_randstate() == old_state;
    p.upper = 1.0000001788139343;
    valid &= p.randomize() && p.x > p.lower && p.x < p.upper && real'(p.y) == 1.0000001192092896;
    p.lower = $bitstoreal(64'h7ff8000000000001);
    old_x = p.x;
    old_y = p.y;
    old_state = p.get_randstate();
    valid &= !p.randomize() && p.x == old_x && p.y == old_y && p.get_randstate() == old_state;
    p.bounds.constraint_mode(0);
    p.x = $bitstoreal(64'h7ff8000000000001);
    p.y = $bitstoshortreal(32'd2143289345);
    p.x.rand_mode(0);
    p.y.rand_mode(0);
    old_state = p.get_randstate();
    valid &= p.randomize(null) && $realtobits(p.x) == 64'h7ff8000000000001 &&
             $shortrealtobits(p.y) == 32'd2143289345 && p.get_randstate() == old_state;
    valid &= p.randomize(x) && p.x.rand_mode() == 0 && p.y.rand_mode() == 0 &&
             $shortrealtobits(p.y) == 32'd2143289345 &&
             ($realtobits(p.x) & 64'h7ff0000000000000) != 64'h7ff0000000000000;
    p.lower = 0.0;
    p.upper = 2.0;
    p.bounds.constraint_mode(1);
    old_x = p.x;
    old_state = p.get_randstate();
    valid &= !p.randomize() && p.x == old_x && $shortrealtobits(p.y) == 32'd2143289345 &&
             p.get_randstate() == old_state;
    p.x.rand_mode(1);
    p.y.rand_mode(1);
    valid &= p.randomize() && p.x > 0.0 && p.x < 2.0 && p.y > 0.0 && p.y < 2.0;
    repeat (8) begin
      status = std::randomize(sx, sy) with { sx > sl; sx < su; sy > sl; sy < su; };
      valid &= status && sx > sl && sx < su && sy > sl && sy < su;
    end
    $display("real source valid %0d density %0d", valid, density);
  end
endmodule
