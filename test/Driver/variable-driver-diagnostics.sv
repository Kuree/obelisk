// RUN: not obelisk --std=1800-2017 -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2017 Clause 6.5: variables may be written by one continuous
// assignment, one primitive output, or one module/interface output, but those
// forms cannot be mixed with procedural assignments or duplicated.
module multiple_continuous;
  int value;
  assign value = 12;
  assign value = 13;
endmodule

module mixed_continuous_procedural;
  logic clock;
  int value;
  assign value = 12;
  always @(posedge clock)
    value <= ~value;
endmodule

// CHECK: error: cannot have multiple continuous assignments to variable 'value'
// CHECK: error: cannot mix continuous and procedural assignments to variable 'value'
