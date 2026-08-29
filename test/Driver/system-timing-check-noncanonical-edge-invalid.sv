// RUN: not obelisk -fno-lto -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_noncanonical_edge_invalid(
    input wire data, reference);
  specify
    $setup(edge [01] data, posedge reference, 1);
  endspecify
endmodule

// A proper subset of Clause 31.5 transitions cannot be reconstructed from
// the scheduler's standard edge classes and must remain explicitly rejected.
// CHECK: error: IEEE 1800-2017 Clause 31 system timing checks are retained in semantic IR but are not executable yet
