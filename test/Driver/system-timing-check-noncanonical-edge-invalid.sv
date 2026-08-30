// RUN: not obelisk -fno-lto -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_noncanonical_edge_invalid(input wire clock);
  specify
    // The current static $width actor derives one canonical opposite edge.
    // A Clause 31.5 subset needs a separate inverse-descriptor audit.
    $width(edge [01, 0x] clock, 1);
  endspecify
endmodule

// CHECK: error: IEEE 1800-2017 Clause 31 system timing checks are retained in semantic IR but are not executable yet
