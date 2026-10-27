// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 11.5.1, 18.4, 18.5.12 and 18.8: variable indexed
// part-selects preserve value, X and Z planes in ascending and descending
// dimensions. Frozen array elements retain their own masks through a nested
// range. Unknown, partial and wide out-of-bounds addresses remain errors
// across casts; state guards and block modes can discard those errors.
// CHECK: variable packed range planes PASS

typedef bit [3:0] B4;

class R;
  rand logic [7:0] down;
  rand logic [0:7] up;
  rand logic [7:0] a[-1:0];
  rand B4 du, dd, uu, ud, picked, nz, nx;
  int index;
  int ai;
  bit enable = 1;
  bit active;
  constraint fixed {
    a[0] == 8'd165;
    if (active) {
      down == 8'd165;
      up == 8'd165;
    }
  }
  constraint selected {
    if (enable) {
      du == B4'(down[index +: 4]);
      dd == B4'(down[index -: 4]);
      uu == B4'(up[index +: 4]);
      ud == B4'(up[index -: 4]);
      picked == B4'(a[ai][index +: 4]);
      nz == $countbits(down[index +: 4], 1'bz);
      nx == $countbits(down[index +: 4], 1'bx);
    }
  }
endclass

class W;
  rand bit [7:0] x;
  rand B4 out;
  bit [95:0] index;
  constraint selected {
    x == 8'd165;
    out == x[index +: 4];
  }
endclass

module top;
  R r;
  W w;
  B4 saved;
  initial begin
    r = new;
    r.down = 8'b10xz0110;
    r.up = 8'b10xz0110;
    r.down.rand_mode(0);
    r.up.rand_mode(0);
    r.a[-1] = 8'b10xz0110;
    r.a[-1].rand_mode(0);
    r.ai = -1;
    for (int iteration = 0; iteration < 12; iteration++) begin
      r.index = 3;
      if (!r.randomize() || r.du != 0 || r.dd != 6 || r.uu != 3 || r.ud != 8 ||
          r.picked != 0 || r.nz != 1 || r.nx != 1)
        $fatal(1, "frozen variable ranges three");
      r.index = 4;
      if (!r.randomize() || r.du != 8 || r.dd != 3 || r.uu != 6 || r.ud != 0 ||
          r.picked != 8 || r.nz != 1 || r.nx != 1)
        $fatal(1, "frozen variable ranges four");
      if (r.down !== 8'b10xz0110 || r.up !== 8'b10xz0110 ||
          r.a[-1] !== 8'b10xz0110 || r.a[0] != 8'd165)
        $fatal(1, "frozen ranged source changed");
    end
    saved = r.du;
    r.index = 2;
    if (r.randomize() || r.du != saved || r.a[0] != 8'd165)
      $fatal(1, "partial low range committed");
    r.index = 5;
    if (r.randomize() || r.du != saved || r.down !== 8'b10xz0110)
      $fatal(1, "partial high range committed");
    r.index = 32'bx;
    if (r.randomize() || r.du != saved)
      $fatal(1, "unknown ranged address consumed by cast");
    r.enable = 0;
    if (!r.randomize())
      $fatal(1, "state guard did not discard range error");
    r.enable = 1;
    r.selected.constraint_mode(0);
    if (!r.randomize())
      $fatal(1, "disabled range block evaluated");
    r.selected.constraint_mode(1);
    r.index = 3;
    r.ai = 1;
    if (r.randomize() || r.a[-1] !== 8'b10xz0110 || r.a[0] != 8'd165)
      $fatal(1, "nested array range bounds committed");
    r.ai = 0;
    r.active = 1;
    r.down.rand_mode(1);
    r.up.rand_mode(1);
    if (!r.randomize() || r.du != 4 || r.dd != 5 || r.uu != 2 || r.ud != 10 ||
        r.picked != 4 || r.nz != 0 || r.nx != 0)
      $fatal(1, "active variable ranges three");
    r.index = 4;
    if (!r.randomize() || r.du != 10 || r.dd != 2 || r.uu != 5 || r.ud != 4 ||
        r.picked != 10 || r.a[-1] !== 8'b10xz0110)
      $fatal(1, "active variable ranges four");

    w = new;
    w.index = 0;
    if (!w.randomize() || w.out != 5)
      $fatal(1, "wide range zero index");
    w.index = 4;
    if (!w.randomize() || w.out != 10)
      $fatal(1, "wide range high index");
    w.index = (96'h1 << 65) | 4;
    if (w.randomize() || w.out != 10 || w.x != 8'd165)
      $fatal(1, "wide range index truncated");
    $display("variable packed range planes PASS");
    $finish;
  end
endmodule
