// RUN: obelisk -O3 --vpi=off --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O3 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.eval > %t.eval.out
// RUN: %t.generic > %t.generic.out
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.eval.out
// RUN: diff -u %t.generic.out %t.eval.out
// RUN: FileCheck %s < %t.eval.out

// IEEE 1800-2023 7.2.1: preserve the packed bit order, shared boundary bytes,
// and two-state member conversion. 7.4.5: invalid array writes do nothing.
module native_packed_access_units;
  typedef struct packed {
    logic [31:0] e;
    bit [3:0] d;
    logic [6:0] c;
    logic [18:0] b;
    logic [31:0] a;
    logic pad;
  } record_t;
  record_t values[3];
  record_t result;
  integer selected;
  logic [31:0] input_value;
  logic [18:0] input_b;
  logic [31:0] words[8];
  logic [31:0] word_read;
  logic [2:0] lanes[40];
  logic [2:0] lane_read;
  always_comb word_read = words[selected];
  always_comb lane_read = lanes[selected];

  function automatic record_t local_update(record_t initial_value, integer idx);
    record_t slots[3];
    for (int i = 0; i < 3; ++i)
      slots[i] = initial_value ^ 95'(i);
    slots[idx].a = input_value;
    slots[idx].b = input_b;
    slots[idx].c = 7'b10xz101;
    slots[idx].d = 4'bxz11;
    return slots[0] ^ slots[1] ^ slots[2];
  endfunction
  always_comb result = local_update(values[0], selected);

  initial begin
    for (int i = 0; i < 3; ++i) values[i] = '1;
    for (int i = 0; i < 8; ++i) words[i] = 32'h12345678 ^ 32'(i);
    for (int i = 0; i < 40; ++i) lanes[i] = 3'(i);
    input_value = 32'h12345678; input_b = 19'h23456; selected = 1;
    #1;
    values[1].a = 32'h89abcdef;
    values[1].b = 19'h45678;
    values[1].d = 4'bxz11;
    if (values[0] !== '1 || values[2] !== '1 ||
        values[1].pad !== 1'b1 || values[1].c !== 7'h7f ||
        values[1].e !== 32'hffffffff || values[1].d !== 4'b0011)
      $fatal(1, "packed neighbor or padding changed");
    $display("valid %b %b", values[1], result);
    input_value = 'x; input_b = 'z; selected = 2;
    #1 $display("unknown %b", result);
    selected = -1;
    #1;
    words[selected] = '1;
    lanes[selected] = '1;
    if (lane_read !== 3'bxxx) $fatal(1, "invalid narrow read");
    $display("negative %b %b", result, word_read);
    selected = 3;
    #1 $display("past-end %b", result);
    selected = 'x;
    #1;
    words[selected] = '1;
    lanes[selected] = '1;
    if (lane_read !== 3'bxxx) $fatal(1, "unknown narrow index");
    $display("unknown-index %b %b", result, word_read);
    for (int i = 0; i < 8; ++i)
      if (words[i] !== (32'h12345678 ^ 32'(i)))
        $fatal(1, "invalid word index changed state");
    selected = 39;
    #1;
    lanes[selected] = 3'bx1z;
    #1;
    if (lane_read !== 3'bx1z) $fatal(1, "last narrow element");
    selected = 40;
    #1;
    lanes[selected] = '1;
    if (lane_read !== 3'bxxx) $fatal(1, "past-end narrow index");
    for (int i = 0; i < 39; ++i)
      if (lanes[i] !== 3'(i)) $fatal(1, "narrow neighbor changed");
    if (lanes[39] !== 3'bx1z) $fatal(1, "past-end narrow write");
    // A bitstream view remains compatible with field writes.
    values[1][63:59] = 5'b01010;
    if (values[1].d !== 4'b1010 || values[1].a !== 32'h89abcdef ||
        values[1].b !== 19'h45678)
      $fatal(1, "packed bitstream view changed unrelated fields");
    for (int i = 0; i < 3; ++i) begin
      values[i].a = 32'h11223344 ^ 32'(i);
      values[i].b = 19'h12345 ^ 19'(i);
      values[i].c = 7'bx01z110;
    end
    $display("indexed %b %b %b", values[0], values[1], values[2]);
    $display("PASS");
    $finish;
  end
endmodule
// CHECK: valid
// CHECK: unknown
// CHECK: negative
// CHECK: past-end
// CHECK: unknown-index
// CHECK: indexed
// CHECK: PASS
