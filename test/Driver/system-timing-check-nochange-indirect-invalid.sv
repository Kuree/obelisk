// RUN: not obelisk -O0 %s -o %t 2>&1 | FileCheck %s

module system_timing_check_nochange_indirect_invalid(
    input wire reference, select,
    input wire [1:0] data);
  specify
    $nochange(posedge reference, data[select], 0, 0);
  endspecify
endmodule

// The static actor subscribes directly to one whole Clause 31.8 handle;
// runtime-selected data expressions remain diagnosed.
// CHECK: error: basic timing-check event is not a direct signal handle
