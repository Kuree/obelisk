// RUN: obelisk --vpi=off -O3 --native-scheduler=eval %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk --vpi=off -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

// Unpacked elements use LLVM allocation strides. Packed fields and language
// bit-streams still have their exact bit widths (1800-2023 7.2, 7.4, 6.24.3).
module native_unpacked_layout;
  typedef logic [2:0] narrow_t;
  typedef narrow_t array_t [3:0];
  typedef struct {
    logic flag;
    logic [23:0] word;
    bit [6:0] bits;
    array_t nested;
  } record_t;
  typedef logic [43:0] stream_t;
  array_t a, b;
  record_t records [2];
  record_t copy;
  record_t untouched;
  logic [23:0] words [5];
  narrow_t queue_values [$];
  stream_t stream;
  logic [11:0] narrow_stream, observed_stream;
  integer index;
  narrow_t selected;
  narrow_t lanes[64], transformed[64];
  narrow_t copied[63:0];
  narrow_t rows[2][11], reduced[11];
  logic [66:0][2:0] packed_lanes, packed_result;
  narrow_t from_packed[66:0];
  logic [66:0] packed_flags, packed_flags_result, packed_flags_xor;
  logic unpacked_flags[67];
  always_comb selected = a[index];
  always_comb begin
    for (int i = 0; i < 64; ++i) transformed[i] = lanes[i] ^ 3'b101;
  end
  always_comb begin
    for (int i = 0; i < 64; ++i) copied[i] = lanes[i];
  end
  always_comb begin
    for (int i = 0; i < 11; ++i) begin
      reduced[i] = 0;
      for (int j = 0; j < 2; ++j) reduced[i] ^= rows[j][i];
    end
  end
  always_comb begin
    for (int i = 0; i < 67; ++i) begin
      from_packed[i] = packed_lanes[i];
      unpacked_flags[i] = packed_flags[i];
    end
  end
  always_comb begin
    for (int i = 0; i < 67; ++i) begin
      packed_result[i] = from_packed[i];
      packed_flags_result[i] = unpacked_flags[i];
      packed_flags_xor[i] = unpacked_flags[i] ^ packed_flags[i];
    end
  end

  initial begin
    if ($bits(a) != 12 || $bits(copy) != 44) $fatal(1, "padding entered $bits");
    if (records[1].word !== 24'hxxxxxx || records[1].bits !== 0)
      $fatal(1, "unpacked defaults");
    a = '{3'b001, 3'bx1z, 3'b101, 3'b010};
    b = a;
    narrow_stream = {>>{a}};
    if (narrow_stream !== 12'b001_x1z_101_010) $fatal(1, "array stream");
    {>>{b}} = 12'b101_011_001_110;
    if (b !== array_t'{3'b101, 3'b011, 3'b001, 3'b110}) $fatal(1, "stream import");
    for (int i = 0; i < 5; i++) words[i] = 24'h123456 ^ 24'(i);
    for (int i = 0; i < 2; i++) begin
      records[i].flag = 1;
      records[i].word = words[i];
      records[i].bits = 7'h55;
      records[i].nested = a;
    end
    index = 1;
    for (int i = 0; i < 64; ++i) lanes[i] = 3'(i);
    lanes[7] = 3'bx1z;
    for (int i = 0; i < 11; ++i) begin
      rows[0][i] = 3'(i);
      rows[1][i] = 3'(i >> 1);
    end
    rows[0][7] = 3'bx1z;
    rows[1][10] = 3'bz0x;
    for (int i = 0; i < 67; ++i) packed_lanes[i] = 3'(i);
    packed_lanes[7] = 3'bx1z;
    packed_lanes[63] = 3'bz0x;
    packed_lanes[66] = 3'b1zx;
    packed_flags = 64'hfa62_11c3_5198_be0d;
    packed_flags[7] = 'x;
    packed_flags[63] = 'z;
    packed_flags[66] = 'x;
    #1;
    for (int i = 0; i < 64; ++i)
      if (transformed[i] !== (lanes[i] ^ 3'b101) || copied[i] !== lanes[i])
        $fatal(1, "word loop crossed array padding");
    for (int i = 0; i < 11; ++i)
      if (reduced[i] !== (rows[0][i] ^ rows[1][i]))
        $fatal(1, "nested word loop crossed array padding");
    if (packed_result !== packed_lanes || packed_flags_result !== packed_flags ||
        packed_flags_xor !== (packed_flags ^ packed_flags))
      $fatal(1, "mixed packed/unpacked word lanes");
    for (int i = 0; i < 67; ++i)
      if (from_packed[i] !== packed_lanes[i] || unpacked_flags[i] !== packed_flags[i])
        $fatal(1, "mixed word lane reversal or padding");
    if (selected !== 3'b101) $fatal(1, "descending array address");
    copy = records[index];
    stream = stream_t'(copy);
    if (stream !== {1'b1, 24'h123457, 7'h55, narrow_stream})
      $fatal(1, "record stream padding");
    records[index].nested[2] <= 3'b101;
    words[index] <= 24'hfedcba;
    #1;
    if (records[1].nested[2] !== 3'b101 || records[0].nested !== a ||
        words[1] !== 24'hfedcba || words[2] !== 24'h123454)
      $fatal(1, "deferred element store");
    b = '{default: 3'b111};
    b.reverse();
    if (b !== array_t'{default: 3'b111}) $fatal(1, "padded splat or fixed transfer");
    b = '{3'b001, 3'b010, 3'b011, 3'b100};
    b.reverse();
    if (b !== array_t'{3'b100, 3'b011, 3'b010, 3'b001}) $fatal(1, "fixed reverse");
    queue_values = b;
    if (queue_values[0] !== 3'b100 || queue_values[3] !== 3'b001)
      $fatal(1, "container transfer");
    index = -1;
    #1;
    copy = records[index];
    if (copy !== untouched || copy.bits !== 0 ||
        $countbits(copy, 1'bx) != 37 || $countbits(copy, 1'b0) != 7)
      $fatal(1, "invalid aggregate default or padding");
    a[index] = 3'b111;
    observed_stream = {>>{a}};
    if (selected !== 3'bxxx || observed_stream !== narrow_stream)
      $fatal(1, "invalid element access");
    index = 'x;
    #1;
    a[index] = 3'b000;
    observed_stream = {>>{a}};
    if (selected !== 3'bxxx || observed_stream !== narrow_stream)
      $fatal(1, "unknown element access");
    $display("PASS unpacked layout %b", stream);
    $finish;
  end
endmodule
// CHECK: PASS unpacked layout
