// RUN: not %talos -emit-slang %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 19.5.2: a transition bin of length 0, a single value range
// that is not repeated or can be repeated exactly once, is illegal. Bins
// that span a transition are accepted.
module top;
  bit [1:0] x;
  localparam int ONE = 1;
  covergroup g;
    coverpoint x {
      bins single = (0);
      bins range = ([0:1]);
      bins once = (1[*ONE]);
      bins maybe = (2[*1:2]);
      bins twice = (0[*2]);
      bins step = (0 => 1);
      bins go = (1[->1]);
    }
  endgroup
  g c = new;
endmodule

// CHECK-DAG: error: transition bin 'single' has a transition of length 0
// CHECK-DAG: error: transition bin 'range' has a transition of length 0
// CHECK-DAG: error: transition bin 'once' has a transition of length 0
// CHECK-DAG: error: transition bin 'maybe' has a transition of length 0
// CHECK-NOT: transition bin 'twice'
// CHECK-NOT: transition bin 'step'
// CHECK-NOT: transition bin 'go'
