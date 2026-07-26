// RUN: obelisk -DREAL_LITERAL -emit-sim %s | FileCheck %s --check-prefix=LITERAL
// RUN: obelisk -DREAL_ARITHMETIC -emit-sim %s | FileCheck %s --check-prefix=ARITHMETIC
// RUN: obelisk -DSHORTREAL_VALUE -emit-sim %s | FileCheck %s --check-prefix=SHORTREAL
// RUN: obelisk -DREAL_AGGREGATE -emit-sim %s | FileCheck %s --check-prefix=AGGREGATE
// RUN: not obelisk -DREAL_NET -emit-sim %s 2>&1 | FileCheck %s --check-prefix=NET
// RUN: obelisk -DREAL_PORT -emit-sim %s | FileCheck %s --check-prefix=PORT
// RUN: not obelisk -DREAL_DPI -emit-sim %s 2>&1 | FileCheck %s --check-prefix=DPI

`ifdef REAL_LITERAL
module real_literal;
  real value;
  initial value = 1.5;
endmodule
`endif

`ifdef REAL_ARITHMETIC
module real_arithmetic;
  real lhs;
  real rhs;
  initial lhs = lhs + rhs;
endmodule
`endif

`ifdef SHORTREAL_VALUE
module shortreal_value;
  shortreal value;
  initial value = 0;
endmodule
`endif

`ifdef REAL_AGGREGATE
module real_aggregate;
  real values[2];
endmodule
`endif

`ifdef REAL_NET
module unsupported_real_net;
  wire real value;
endmodule
`endif

`ifdef REAL_PORT
module real_port(input real value);
endmodule
`endif

`ifdef REAL_DPI
module unsupported_real_dpi;
  import "DPI-C" function real pass_real(input real value);
  real value;
  initial value = pass_real(value);
endmodule
`endif

// LITERAL: arith.constant 1.500000e+00 : f64
// ARITHMETIC: arith.addf
// SHORTREAL: obelisk_sim.storage.decl
// SHORTREAL-SAME: : f32
// AGGREGATE: !obelisk_sim.unpacked_array<0 : 1 x f64>
// NET: 'real' is not a valid type for a net
// PORT: obelisk_sim.storage.decl
// PORT-SAME: : f64
// DPI: DPI imports support only scalar predefined integers
