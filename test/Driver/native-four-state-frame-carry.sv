// A four-state value that stays live across a suspension is held in the
// canonical process frame, which packs its value and unknown planes at the
// target size of the value type. The bytecode register file keeps the same
// two planes one limb apart, so the tiers must marshal the planes separately.
// Copying one contiguous run instead turns x and z into 0 for every logic
// value narrower than a limb.
// RUN: obelisk -fno-lto --native-scheduler=generic %s -o %t.native
// RUN: obelisk -fno-lto --native-scheduler=aot %s -o %t.aot
// RUN: obelisk -fno-lto --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.native > %t.native.out
// RUN: %t.aot > %t.aot.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.out %t.aot.out
// RUN: FileCheck %s < %t.native.out

module native_four_state_frame_carry;
  logic clock = 0;
  logic started = 0;
  logic [7:0] wide;
  logic narrow;

  always begin
    automatic logic [7:0] carried_wide;
    automatic logic carried_narrow;
    carried_wide = {4'b1010, wide[3:0]};
    carried_narrow = narrow;
    @(wide);
    $display("wide=%b narrow=%b", carried_wide, carried_narrow);
  end

  always_ff @(posedge clock)
    if (!started) begin
      wide <= 8'd0;
      narrow <= 1'b0;
      started <= 1'b1;
    end

  initial begin
    #1 clock = 1;
    #1 clock = 0;
    $finish;
  end
endmodule

// The sampled halves were unknown before the first clock and must remain so.
// CHECK: wide=1010xxxx narrow=x
