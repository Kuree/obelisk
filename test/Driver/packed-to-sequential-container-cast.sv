// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 6.24.1 and 11.4.14: a bit-stream cast can materialize a
// dynamically sized sequential container, consuming the packed source from
// most-significant to least-significant element.
module packed_to_sequential_container_cast;
  typedef bit bit_queue_t[$];
  typedef byte byte_queue_t[$];
  typedef logic [3:0] logic_queue_t[$];
  typedef byte byte_dynamic_t[];
  typedef bit bounded_queue_t[$:2];

  bit_queue_t bits;
  byte_queue_t bytes;
  logic_queue_t four_state;
  byte_dynamic_t dynamic_bytes;
  bounded_queue_t bounded;
  logic [7:0] source4;

  initial begin
    bits = bit_queue_t'(4'he);
    $display("bits=%0d:%0d%0d%0d%0d", bits.size(), bits[0], bits[1],
             bits[2], bits[3]);

    bytes = byte_queue_t'(24'habcdef);
    $display("bytes=%0d:%h,%h,%h", bytes.size(), bytes[0], bytes[1],
             bytes[2]);

    dynamic_bytes = byte_dynamic_t'(24'h123456);
    $display("dynamic=%0d:%h,%h,%h", dynamic_bytes.size(), dynamic_bytes[0],
             dynamic_bytes[1], dynamic_bytes[2]);

    source4 = 8'b10xz_01zx;
    four_state = logic_queue_t'(source4);
    $display("four=%0d:%b,%b", four_state.size(), four_state[0],
             four_state[1]);

    bounded = bounded_queue_t'(4'he);
    $display("bounded=%0d:%0d%0d%0d", bounded.size(), bounded[0], bounded[1],
             bounded[2]);
  end
endmodule

// CHECK: bits=4:1110
// CHECK-NEXT: bytes=3:ab,cd,ef
// CHECK-NEXT: dynamic=3:12,34,56
// CHECK-NEXT: four=2:10xz,01zx
// CHECK-NEXT: bounded=3:111
