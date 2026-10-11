// RUN: obelisk -emit-slang %s | not obelisk-opt --convert-slang-to-obelisk --lower-obelisk-to-sim=opt-level=0 -o /dev/null 2>&1 | FileCheck %s

// randc properties wider than 32 bits are rejected (IEEE 1800-2023 18.4.2
// lets an implementation limit randc to no fewer than 8 bits).
class C;
  // CHECK: [[@LINE+1]]:{{[0-9]+}}: error: randc properties must be packed integral values no wider than 32 bits
  randc bit [32:0] x;
endclass
module top;
  initial begin
    C c = new;
    void'(c.randomize());
  end
endmodule
