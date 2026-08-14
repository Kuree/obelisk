// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: diff -u %t.o0.native.err %t.o3.bytecode.err
// RUN: FileCheck %s < %t.o3.native.out

module native_pure_system_functions;
  typedef struct packed {
    logic [6:0] left;
    bit [4:0] right;
  } packed_pair_t;
  typedef struct {
    byte left;
    logic [4:0] right;
  } unpacked_pair_t;
  typedef union packed {
    logic [6:0] left;
    bit [6:0] right;
  } packed_union_t;
  typedef union {
    byte left;
    logic [12:0] right;
  } unpacked_union_t;

  int side_effects;
  logic [11:0] mixed;
  logic [129:0] wide;
  logic [3:0] words [0:1];
  logic state_control;
  logic [7:0] raw;

  function automatic int bump();
    side_effects++;
    return side_effects;
  endfunction

  initial begin
    side_effects = 0;
    $display("bits=%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d side=%0d",
             $bits(int), $bits(real), $bits(shortreal), $bits(time),
             $bits(logic [2:0][4:0]), $bits(packed_pair_t),
             $bits(unpacked_pair_t), $bits(packed_union_t),
             $bits(unpacked_union_t), $bits(wide), $bits(words),
             $bits(bump()), side_effects);

    mixed = 12'b01xz_1100_xz01;
    state_control = 1'bx;
    $display("counts=%0d,%0d,%0d,%0d,%0d",
             $countbits(mixed, 1'b0), $countbits(mixed, 1'b1),
             $countbits(mixed, state_control),
             $countbits(mixed, 1'bz),
             $countbits(mixed, 1'b0, 1'b1));
    $display("count-special=%0d,%0d,%0d,%0d",
             $countbits(mixed, 1'b1, 1'b1), $countones(mixed),
             $countbits(mixed, 2'b10), $countbits(mixed, 2'bx1));

    $display("predicates=%0d,%0d,%0d,%0d,%0d",
             $onehot(4'bx001), $onehot(4'bx011),
             $onehot0(4'bxz00), $isunknown(4'b10z1),
             $isunknown(4'b1011));

    wide = '0;
    wide[0] = 1'b1;
    wide[64] = 1'b1;
    wide[129] = 1'b1;
    $display("wide=%0d,%0d", $countones(wide), $clog2(wide));
    wide[0] = 1'b0;
    wide[64] = 1'b0;
    $display("wide-power=%0d", $clog2(wide));

    words[0] = 4'b10xz;
    words[1] = 4'b0110;
    $display("unpacked=%0d,%0d", $countones(words),
             $countbits(words, 1'bx, 1'bz));

    $display("clog2=%0d,%0d,%0d,%0d,%0d,%0d,%0d",
             $clog2(0), $clog2(1), $clog2(2), $clog2(3),
             $clog2(255), $clog2(-3'sb1), $clog2(3'bz1x));

    raw = 8'hff;
    $display("casts=%0d,%0d,%h,%h,%h",
             $signed(raw) < 0, $unsigned($signed(raw)) > 0,
             $unsigned($signed(raw)), $signed(8'h80) >>> 1,
             $unsigned(8'h80) >> 1);
  end
endmodule

// CHECK: bits=32,64,32,64,15,12,13,7,13,130,8,32 side=0
// CHECK-NEXT: counts=4,4,2,2,8
// CHECK-NEXT: count-special=4,4,4,4
// CHECK-NEXT: predicates=1,0,1,1,0
// CHECK-NEXT: wide=3,130
// CHECK-NEXT: wide-power=129
// CHECK-NEXT: unpacked=3,2
// CHECK-NEXT: clog2=0,0,1,2,8,3,1
// CHECK-NEXT: casts=1,1,ff,c0,40
