// RUN: not obelisk -O0 %s -o %t.native 2>&1 | FileCheck %s
// RUN: not obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode 2>&1 \
// RUN:   | FileCheck %s

class item;
endclass

typedef union {
  item object;
  longint bits;
} unsafe_union;

module top;
  unsafe_union value;

  initial begin
    value.bits = 123;
    $display("%0d", value.object != null);
  end
endmodule

// A flat root-offset list cannot distinguish the active union member. Reject
// this until the trace ABI can guard roots by the active member.
// CHECK: error:
// CHECK-SAME: storage must have{{( a)?}} fixed{{( packed)?}} width
