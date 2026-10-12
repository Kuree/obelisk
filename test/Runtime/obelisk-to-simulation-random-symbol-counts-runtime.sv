// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.8 and 20.9: the control arguments of $countbits form a
// set of 0/1/X/Z symbols taken from their LSBs. The compiled planes keep
// stored Z distinct from arithmetic X through captures, unary operators,
// casts, shifts, concatenation, replication and conditional merging (11.4.3,
// 11.4.11). Active candidates stay two-state; frozen scalar and 130-bit
// properties retain their symbols.

class C;
  rand logic [3:0] x;
  rand int n0, n1, nx, nz, nu, na, nc, lg;
  rand int nznot, nxnot, nzshift, nzconditional, nzsigned, ndivx, ndivz;
  rand bit unk, one, one0;
  logic [3:0] stateBits;
  logic [1:0] control;
  logic condition;
  bit activeCase;
  constraint rules {
    $countbits(+x, 1'bz) == 0;
    $countbits(+x, 1'bx) == 4 * unk;
    $countbits({2{x}}, 1'bz) == nz + nz;
    if (activeCase) x == 4'b1000;
    n0 == $countbits(x, 1'b0, 1'b0);
    n1 == $countbits(x, 1'b1, 1'b1);
    nx == $countbits(x, 1'bx, 1'bx);
    nz == $countbits(x, 1'bz);
    nu == $countbits(x, 1'bx, 1'bz, 1'bx);
    na == $countbits(x, 1'b0, 1'b1, 1'bx, 1'bz);
    nc == $countbits({x, stateBits}, control, control);
    lg == $clog2($signed(x));
    nznot == $countbits(~x, 1'bz);
    nxnot == $countbits(~x, 1'bx);
    nzshift == $countbits(x << 1, 1'bz);
    nzconditional == $countbits(condition ? x : stateBits, 1'bz);
    nzsigned == $countbits($signed(x), 1'bz);
    ndivx == $countbits(x / 4'b0, 1'bx);
    ndivz == $countbits(x / 4'b0, 1'bz);
    unk == $isunknown(x);
    one == $onehot(x);
    one0 == $onehot0(x);
  }
endclass

class W;
  rand logic [129:0] x;
  rand int nx, nz, nu, na, lg, nzsigned;
  constraint rules {
    nx == $countbits(x, 1'bx);
    nz == $countbits(x, 1'bz, 1'bz);
    nu == $countbits(x, 1'bx, 1'bz);
    na == $countbits(x, 1'b0, 1'b1, 1'bx, 1'bz);
    lg == $clog2($signed(x));
    nzsigned == $countbits($unsigned(x), 1'bz);
  }
endclass

module top;
  C c;
  W w;
  int ok;

  task automatic show(string tag);
    $display("%s ok=%0d n0=%0d n1=%0d nx=%0d nz=%0d nu=%0d na=%0d nc=%0d lg=%0d",
             tag, ok, c.n0, c.n1, c.nx, c.nz, c.nu, c.na, c.nc, c.lg);
    $display("%s not=%0d,%0d shift=%0d cond=%0d signed=%0d div=%0d,%0d flags=%0d%0d%0d x=%b s=%b",
             tag, c.nznot, c.nxnot, c.nzshift, c.nzconditional, c.nzsigned,
             c.ndivx, c.ndivz, c.unk, c.one, c.one0, c.x, c.stateBits);
  endtask

  initial begin
    c = new;
    c.x = 4'b10xz;
    c.x.rand_mode(0);
    c.stateBits = 4'bzxx1;
    c.control = 2'b1z;
    c.condition = 1'bx;
    ok = c.randomize();
    show("frozen");
    // CHECK: frozen ok=1 n0=1 n1=1 nx=1 nz=1 nu=2 na=4 nc=2 lg=3
    // CHECK-NEXT: frozen not=0,2 shift=1 cond=0 signed=1 div=4,0 flags=111 x=10xz s=zxx1

    // randomize(null) checks the frozen state against the constraints.
    ok = c.randomize(null);
    $display("checker ok=%0d", ok);
    // CHECK-NEXT: checker ok=1

    // A control whose LSB is X counts X symbols; condition 1 selects x.
    c.control = 2'b0x;
    c.condition = 1'b1;
    ok = c.randomize();
    $display("xcontrol ok=%0d nc=%0d cond=%0d nz=%0d nx=%0d",
             ok, c.nc, c.nzconditional, c.nz, c.nx);
    // CHECK-NEXT: xcontrol ok=1 nc=3 cond=1 nz=1 nx=1

    // Only the control LSB (0) matters; condition 0 selects stateBits.
    c.control = 2'b10;
    c.condition = 1'b0;
    ok = c.randomize();
    $display("lsb ok=%0d nc=%0d cond=%0d", ok, c.nc, c.nzconditional);
    // CHECK-NEXT: lsb ok=1 nc=1 cond=1

    // Once x is active it is two-state: no X/Z symbols are counted.
    c.x.rand_mode(1);
    c.activeCase = 1;
    c.control = 2'b11;
    c.condition = 1'bx;
    ok = c.randomize();
    show("active");
    // CHECK-NEXT: active ok=1 n0=3 n1=1 nx=0 nz=0 nu=0 na=4 nc=2 lg=3
    // CHECK-NEXT: active not=0,0 shift=0 cond=0 signed=0 div=4,0 flags=011 x=1000 s=zxx1

    w = new;
    w.x = {2'b10, {31{4'bzzzz}}, 4'bxxxx};
    w.x.rand_mode(0);
    ok = w.randomize();
    $display("wide ok=%0d nx=%0d nz=%0d nu=%0d na=%0d lg=%0d signed=%0d keep=%0d",
             ok, w.nx, w.nz, w.nu, w.na, w.lg, w.nzsigned,
             w.x === {2'b10, {31{4'bzzzz}}, 4'bxxxx});
    // CHECK-NEXT: wide ok=1 nx=4 nz=124 nu=128 na=130 lg=129 signed=124 keep=1
  end
endmodule
