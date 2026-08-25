// RUN: obelisk -emit-slang %s -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2017 21.2.1: arguments remaining after the conversions in a
// format string are formatted as ordinary output-list items.  Slang usefully
// diagnoses the count mismatch, but it is not a reason to reject the design.
// The executable behavior is tested from hand-authored Simulation MLIR in
// simulation-format-file-void-tasks.mlir.

module top;
  initial begin
    string value;
    value = $sformatf("value=%0d", 12, 32'd97);
    $display("[%s]", value);
  end
endmodule

// CHECK: warning: too many arguments provided for format string
