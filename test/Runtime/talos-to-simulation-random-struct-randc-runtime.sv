// REQUIRES: compiled-sampler
// RUN: %talos --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.4: a randc member of a rand unpacked structure cycles
// through its whole domain (four values for two bits) before repeating, and
// each object has its own cycle. Members without a random qualifier keep their
// string and four-state values across both complete cycles.
// CHECK: single member cycle 1 1 1 1 1
// CHECK-NEXT: single member cycle 2 1 1 1 1
// CHECK-NEXT: independent objects 1 1 1

class C;
  rand struct {
    randc bit [1:0] c;
    logic [3:0] crc;
    string text;
  } h;
endclass

module top;
  C object = new;
  C other = new;
  int status;
  bit valid;
  int h_seen;
  int o_seen;
  initial begin
    object.h.crc = 4'bz;
    object.h.text = "keep";
    for (int cycle = 1; cycle <= 2; cycle++) begin
      valid = 1;
      h_seen = 0;
      repeat (4) begin
        status = object.randomize();
        valid &= status == 1;
        h_seen |= 1 << object.h.c;
      end
      $display("single member cycle %0d %0d %0d %0d %0d", cycle, valid,
               h_seen == 15, object.h.crc === 4'bz, object.h.text == "keep");
    end
    // Interleaving draws from a second object must not disturb either cycle.
    valid = 1;
    h_seen = 0;
    o_seen = 0;
    repeat (4) begin
      valid &= object.randomize() == 1;
      valid &= other.randomize() == 1;
      h_seen |= 1 << object.h.c;
      o_seen |= 1 << other.h.c;
    end
    $display("independent objects %0d %0d %0d", valid, h_seen == 15,
             o_seen == 15);
  end
endmodule
