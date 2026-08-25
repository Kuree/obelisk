// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s

`timescale 1ns / 1ns

module invalid_delayed_transitive_path(
    input wire source, output wire destination);
  wire intermediate;
  buf #1 (intermediate, source);
  buf (destination, intermediate);
  specify
    (source +=> destination) = 2;
  endspecify
endmodule

// Collapsing this path onto the destination actor would incorrectly add the
// intermediate delay before the module-path delay. Transitive source closure
// is therefore restricted to zero-delay combinational actors.
// CHECK: error: simple specify path driver must depend only on its declared whole inputs
