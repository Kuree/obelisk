// XFAIL: *
// RUN: %split-file %s %t
// RUN: %obelisk -emit-slang %t/first.sv %t/second.sv -o /dev/null

// IEEE 1800-2023 3.12.1: a declaration left incomplete at the end of a file
// continues into the next file of the compilation unit. Slang v11 parses each
// file on its own unless every file shares one unit, so the module opened in
// first.sv never closes.

//--- first.sv
module top;
//--- second.sv
endmodule
