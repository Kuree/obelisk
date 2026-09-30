// Real-world .f manifests use '+'-separated include directories and defines.
// Their source and include-search order must survive command-file expansion.
// RUN: %split-file %s %t.dir
// RUN: cd %t.dir && obelisk -O0 -f root.f -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: cd %t.dir && obelisk -E --single-unit \
// RUN:   +incdir+first+second +define+ENABLED+VALUE=7 '-DEXPR=1+2' \
// RUN:   'package.sv' main.sv | FileCheck %s --check-prefix=PREPROCESS
// RUN: cd %t.dir && obelisk -E --single-unit -I'first+extra' \
// RUN:   -DENABLED -DVALUE=7 '-DEXPR=1+2' package.sv main.sv \
// RUN:   | FileCheck %s --check-prefix=PREPROCESS
// RUN: not obelisk -E +incdir+ %s 2>&1 | FileCheck %s --check-prefix=EMPTY-INCLUDE
// RUN: not obelisk -E +define+VALUE=7++ENABLED %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EMPTY-MACRO

// CHECK: lists = 7 11 3
// PREPROCESS: parameter int value = 11;
// PREPROCESS: $display("lists = %0d %0d %0d", 7, ordered::value, 1+2);
// EMPTY-INCLUDE: error: empty include directory in '+incdir+'
// EMPTY-MACRO: error: empty macro definition in '+define+VALUE=7++ENABLED'

//--- root.f
--single-unit
--top=manifest_top
+incdir+first+second
+define+ENABLED+VALUE=7
-DEXPR=1+2
-f nested.f
main.sv

//--- nested.f
# The package must precede its importing source.
"package.sv"

//--- package.sv
package ordered;
  `include "value.svh"
  parameter int value = `HEADER_VALUE;
endpackage

//--- first/value.svh
`define HEADER_VALUE 11

//--- second/value.svh
`define HEADER_VALUE 99

//--- first+extra/value.svh
`define HEADER_VALUE 11

//--- main.sv
module manifest_top;
  initial begin
`ifdef ENABLED
    $display("lists = %0d %0d %0d", `VALUE, ordered::value, `EXPR);
`else
    $fatal(1, "ENABLED was not defined");
`endif
    $finish;
  end
endmodule
