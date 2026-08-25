// RUN: obelisk -emit-slang %s -o %t.slang.mlir
// RUN: FileCheck %s --check-prefix=SLANG < %t.slang.mlir
// RUN: obelisk -emit-obelisk %s -o %t.obelisk.mlir
// RUN: FileCheck %s --check-prefix=OBELISK < %t.obelisk.mlir

`timescale 1ns / 1ps

module positive_parallel(input wire source, output wire destination);
  assign destination = source;
  specify
    (source +=> destination) = (2, 3, 4);
  endspecify
endmodule

module negative_parallel(input wire source, output wire destination);
  assign destination = ~source;
  specify
    (source -=> destination) = (5, 6, 7);
  endspecify
endmodule

module positive_full(input wire lhs, input wire rhs, output wire destination);
  or (destination, lhs, rhs);
  specify
    (lhs, rhs +*> destination) = (8, 9, 10);
  endspecify
endmodule

module negative_full(input wire lhs, input wire rhs, output wire destination);
  nand (destination, lhs, rhs);
  specify
    (lhs, rhs -*> destination) = (11, 12, 13);
  endspecify
endmodule

module specify_path_polarity;
  wire source, lhs, rhs;
  wire positive_parallel_out, negative_parallel_out;
  wire positive_full_out, negative_full_out;
  positive_parallel p0(source, positive_parallel_out);
  negative_parallel p1(source, negative_parallel_out);
  positive_full p2(lhs, rhs, positive_full_out);
  negative_full p3(lhs, rhs, negative_full_out);
endmodule

// Slang freezes the standard polarity enumeration and both legal connection
// shapes. This is a source-import test; execution is covered in hand-authored
// Simulation MLIR so it does not depend on frontend syntax behavior.
// SLANG-DAG: timing_connection_full = false
// SLANG-DAG: timing_delay_fs = array<i64: 2000000, 3000000, 4000000>
// SLANG-DAG: timing_polarity = 1 : i32
// SLANG-DAG: timing_connection_full = false
// SLANG-DAG: timing_delay_fs = array<i64: 5000000, 6000000, 7000000>
// SLANG-DAG: timing_polarity = 2 : i32
// SLANG-DAG: timing_connection_full = true
// SLANG-DAG: timing_delay_fs = array<i64: 8000000, 9000000, 10000000>
// SLANG-DAG: timing_polarity = 1 : i32
// SLANG-DAG: timing_connection_full = true
// SLANG-DAG: timing_delay_fs = array<i64: 11000000, 12000000, 13000000>
// SLANG-DAG: timing_polarity = 2 : i32

// All four legal polarity/connection combinations enter the executable static
// path subset. Polarity remains semantic metadata because Clause 30.4.7 says
// it does not modify the modeled data propagation.
// OBELISK-COUNT-4: obelisk.simple_timing_path
