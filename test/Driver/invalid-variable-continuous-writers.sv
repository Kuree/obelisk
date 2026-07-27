// RUN: not obelisk -DMULTIPLE %s -o %t.multiple 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MULTIPLE
// RUN: not obelisk -DMIXED %s -o %t.mixed 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MIXED

module top;
  logic clock;
  int value;

  assign value = 12;

`ifdef MULTIPLE
  assign value = 13;
`endif

`ifdef MIXED
  always @(posedge clock)
    value <= ~value;
`endif
endmodule

// MULTIPLE: error: {{.*}}variable 'top.value' is driven by multiple continuous assignments
// MIXED: error: {{.*}}variable 'top.value' is written by both continuous and procedural assignments
