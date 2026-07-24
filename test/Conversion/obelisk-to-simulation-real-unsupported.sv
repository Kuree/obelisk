// RUN: not obelisk -DREAL_LITERAL -emit-sim %s 2>&1 | FileCheck %s --check-prefix=LITERAL
// RUN: not obelisk -DREAL_ARITHMETIC -emit-sim %s 2>&1 | FileCheck %s --check-prefix=ARITHMETIC
// RUN: not obelisk -DSHORTREAL_VALUE -emit-sim %s 2>&1 | FileCheck %s --check-prefix=SHORTREAL
// RUN: not obelisk -DREAL_AGGREGATE -emit-sim %s 2>&1 | FileCheck %s --check-prefix=AGGREGATE
// RUN: not obelisk -DREAL_NET -emit-sim %s 2>&1 | FileCheck %s --check-prefix=NET
// RUN: not obelisk -DREAL_PORT -emit-sim %s 2>&1 | FileCheck %s --check-prefix=PORT
// RUN: not obelisk -DREAL_DPI -emit-sim %s 2>&1 | FileCheck %s --check-prefix=DPI

`ifdef REAL_LITERAL
module unsupported_real_literal;
  real value;
  initial value = 1.5;
endmodule
`endif

`ifdef REAL_ARITHMETIC
module unsupported_real_arithmetic;
  real lhs;
  real rhs;
  initial lhs = lhs + rhs;
endmodule
`endif

`ifdef SHORTREAL_VALUE
module unsupported_shortreal;
  shortreal value;
  initial value = 0;
endmodule
`endif

`ifdef REAL_AGGREGATE
module unsupported_real_aggregate;
  real values[2];
endmodule
`endif

`ifdef REAL_NET
module unsupported_real_net;
  wire real value;
endmodule
`endif

`ifdef REAL_PORT
module unsupported_real_port(input real value);
endmodule
`endif

`ifdef REAL_DPI
module unsupported_real_dpi;
  import "DPI-C" function real pass_real(input real value);
  real value;
  initial value = pass_real(value);
endmodule
`endif

// LITERAL: real literals are supported only as procedural delay literals
// ARITHMETIC: real arithmetic is not supported
// SHORTREAL: unsupported semantic type in the first simulation slice: '!obelisk.shortreal'
// AGGREGATE: real and realtime are supported only as scalar variables
// NET: 'real' is not a valid type for a net
// PORT: real and realtime ports are not supported
// DPI: DPI imports support only scalar predefined integers
