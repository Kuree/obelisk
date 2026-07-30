// RUN: not obelisk -emit-obelisk %s 2>&1 | FileCheck %s

module unsupported_real_net;
  wire real value;
endmodule

// CHECK: 'real' is not a valid type for a net
