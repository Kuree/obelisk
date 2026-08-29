// RUN: cd %S && obelisk -emit-slang %s -o %t.mlir 2>%t.err
// RUN: FileCheck %s --check-prefix=IR < %t.mlir
// RUN: FileCheck %s --check-prefix=WARN < %t.err

`timescale 1ns / 1ns

module sdf_shared_path_cell(input wire a, b, output wire y);
  assign y = a | b;
  specify (a,b *> y) = 9; endspecify
endmodule

module sdf_shared_path;
  logic a, b;
  wire y;
  sdf_shared_path_cell dut(a, b, y);
  initial $sdf_annotate("Inputs/sdf-shared-path.sdf", dut);
endmodule

// IEEE 1800-2017 32.4.1: a one-port IOPATH must not alter b's shared path.
// IR: timing_delay_fs = array<i64: 9000000>
// IR-NOT: timing_delay_fs = array<i64: 2000000>
// WARN: warning: SDF IOPATH did not match a specify path
