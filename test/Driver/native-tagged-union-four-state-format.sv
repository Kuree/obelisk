// RUN: talos -O3 --top=top %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: talos -O3 --top=top --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

module top;
  typedef union tagged {
    void Invalid;
    logic [7:0] Valid;
    logic signed [79:0] Wide;
  } tagged_t;

  tagged_t value;
  initial begin
    // IEEE 1800-2017 11.9 and 21.2.1.7: %p identifies the active tagged-union
    // member and formats its singular value without discarding four-state or
    // arbitrary-width information.
    value = tagged Valid(8'b10xz01z0);
    $display("valid=%p", value);
    value = tagged Wide(80'sh8abc_def0_1234_5678_9abc);
    $display("wide=%p", value);
    value = tagged Invalid;
    $display("invalid=%p", value);
  end
endmodule

// CHECK: valid='{Valid:X}
// CHECK-NEXT: wide='{Wide:-553755192732873910609220}
// CHECK-NEXT: invalid='{Invalid}
