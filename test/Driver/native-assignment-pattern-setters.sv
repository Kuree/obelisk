// RUN: obelisk -fno-lto -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

module native_assignment_pattern_setters;
  typedef struct packed { logic [3:0] high; logic [7:0] low; } word_t;
  logic [3:0] unpacked[3];
  logic [3:0] packed_bits;
  logic [7:0] replicated[4];
  word_t record;

  initial begin
    unpacked = '{default: 4'ha};
    packed_bits = '{default: 1'b1};
    replicated = '{2{8'h12, 8'h34}};
    record = '{default: '1};
    $display("u=%h%h%h p=%h r=%h%h%h%h s=%h", unpacked[0], unpacked[1],
             unpacked[2], packed_bits, replicated[0], replicated[1], replicated[2],
             replicated[3], record);
    // CHECK: u=aaa p=f r=12341234 s=fff
  end
endmodule
