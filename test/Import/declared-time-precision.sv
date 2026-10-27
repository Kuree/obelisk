// RUN: %split-file %s %t
// RUN: talos -emit-slang %t/unit.sv | FileCheck %s --check-prefix=UNIT
// RUN: talos -emit-slang %t/package.sv | FileCheck %s --check-prefix=PACKAGE
// RUN: talos -emit-slang %t/directive.sv | FileCheck %s --check-prefix=DIRECTIVE
// RUN: talos -emit-slang %t/none.sv | FileCheck %s --check-prefix=NONE

// IEEE 1800-2023 3.14.3: the global time precision is the minimum of every
// timeprecision statement, every timeunit precision argument and the smallest
// `timescale precision in the design. Each counts even when no elaborated
// scope takes its time scale from it, so the importer records the finest one
// it parsed on the root.

//--- unit.sv
timeprecision 1ps;
module top;
  timeunit 1ns/1ns;
endmodule
// UNIT: slang.symbol.root {{.*}}declared_time_precision_fs = 1000 : i64

//--- package.sv
package pkg;
  timeunit 1ns/10fs;
endpackage
module top;
  timeunit 1ns/1ns;
endmodule
// PACKAGE: slang.symbol.root {{.*}}declared_time_precision_fs = 10 : i64

//--- directive.sv
`timescale 1ns/1ps
`timescale 1ns/1ns
module top;
endmodule
// DIRECTIVE: slang.symbol.root {{.*}}declared_time_precision_fs = 1000 : i64

//--- none.sv
module top;
endmodule
// NONE: slang.symbol.root
// NONE-NOT: declared_time_precision_fs
