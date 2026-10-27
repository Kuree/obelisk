// RUN: talos -O0 --execution-tier=native %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: talos -O3 --execution-tier=native %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: talos -O0 --execution-tier=bytecode %s -o %t.bc
// RUN: %t.bc | FileCheck %s

// LRM 6.21, 7.4.5 and 11.5.1: initialization is per invocation; indexed
// field writes preserve neighboring bits and all previously loaded values.
module native_local_aggregate_stack;
  typedef struct packed {
    logic [205:0] payload;
    logic [6:0] tag;
  } record_t;
  typedef record_t ascending_t [2:3];
  typedef record_t descending_t [-2:-3];
  typedef union packed {
    logic [212:0] four;
    bit [212:0] two;
  } mixed_t;
  typedef struct {
    bit [2:0] prefix;
    descending_t records;
    logic [8:0] suffix;
  } nested_t;
  typedef nested_t nested_array_t [-1:0];

  ascending_t state, copied, copied_again;
  bit signed [79:0] copy_index;
  nested_array_t nested_state, nested_copy;
  bit signed [79:0] nested_index;

  always_comb begin
    automatic ascending_t local_value = state;
    automatic ascending_t saved;
    local_value[copy_index].tag = 7'b10xz101;
    saved = local_value;
    local_value[copy_index].tag = 7'h0;
    copied = saved;
    copied_again = saved;
  end

  always_comb begin
    automatic nested_array_t local_value = nested_state;
    local_value[nested_index].records[-3].payload[65:62] = 4'bzx10;
    local_value[nested_index].prefix = 3'b010;
    nested_copy = local_value;
  end

  function automatic void check_nested(input logic signed [79:0] index);
    nested_array_t a = '{default: nested_t'{prefix: 3'b101,
      records: '{213'h123456789abcdef, 213'h76543210fedcba9},
      suffix: 9'b10xz01101}};
    nested_array_t snapshot = a;
    nested_array_t expected = a;
    nested_t selected;
    a[index].records[-3].payload[65:62] = 4'bzx10;
    a[index].records[-2].tag = 7'b10xz101;
    a[index].prefix = 3'b010;
    if (index === -80'sd1 || index === 80'sd0) begin
      expected[index].records[-3].payload[65:62] = 4'bzx10;
      expected[index].records[-2].tag = 7'b10xz101;
      expected[index].prefix = 3'b010;
      selected = a[index];
      if (selected !== expected[index]) $fatal(1, "nested element load");
    end
    if (a !== expected) $fatal(1, "nested update or invalid index");
    if (snapshot[-1].prefix !== 3'b101 ||
        snapshot[0].records[-2].tag !== 7'h6f ||
        snapshot[0].suffix !== 9'b10xz01101)
      $fatal(1, "nested snapshot or padding");
    // Copy a whole object back after writes; X/Z must round trip too.
    a = snapshot;
    if (a !== snapshot) $fatal(1, "nested whole copy");
  endfunction

  function automatic void check(input logic signed [79:0] index);
    ascending_t a = '{213'h123456789abcdef, 213'h76543210fedcba9};
    descending_t d = '{213'h123456789abcdef, 213'h76543210fedcba9};
    ascending_t snapshot = a;
    ascending_t expected = a;
    mixed_t mixed;
    record_t prior_value;
    record_t selected;
    // Include X and Z on opposite sides of a machine-word boundary.
    a[2].payload[65:62] = 4'b1xz0;
    snapshot = a;
    expected = a;
    prior_value = a[index];
    a[index].tag = 7'h55;
    d[-index].tag = 7'h55;
    if (index === 80'sd2) expected[2].tag = 7'h55;
    if (index === 80'sd3) expected[3].tag = 7'h55;
    if (a !== expected) $fatal(1, "field update or snapshot");
    if (snapshot[2].tag !== 7'h6f || snapshot[3].tag !== 7'h29)
      $fatal(1, "old value changed");
    if (index === 80'sd2 || index === 80'sd3) begin
      selected = a[index];
      if (selected.payload !== prior_value.payload || d[-index].tag !== 7'h55)
        $fatal(1, "overlap or descending index");
    end else if (prior_value !== {213{1'bx}})
      $fatal(1, "invalid read default");
    mixed.four = 'x;
    mixed.four[7:0] = 8'h5a;
    if (mixed.two !== 213'h5a) $fatal(1, "two-state union read");
    mixed.two = '1;
    if (mixed.four !== {213{1'b1}}) $fatal(1, "two-state union write");
  endfunction

  initial begin
    nested_state = '{default: nested_t'{prefix: 3'b101,
      records: '{213'h123456789abcdef, 213'h76543210fedcba9},
      suffix: 9'b10xz01101}};
    nested_index = -1;
    state = '{213'h123456789abcdef, 213'h76543210fedcba9};
    copy_index = 3;
    #1;
    if (nested_copy[-1].prefix !== 3'b010 ||
        nested_copy[-1].records[-3].payload[65:62] !== 4'bzx10 ||
        nested_copy[-1].suffix !== 9'b10xz01101 ||
        nested_copy[-1].records[-2] !== nested_state[-1].records[-2] ||
        nested_copy[0] !== nested_state[0])
      $fatal(1, "nested state/private layout conversion");
    if (copied !== copied_again || copied[3].tag !== 7'b10xz101 ||
        copied[2] !== state[2] || copied[3].payload !== state[3].payload)
      $fatal(1, "state/private copy or snapshot after write");
    force copied = '{213'h33, 213'h44};
    state[3].payload[65:62] = 4'bzx10;
    #1;
    if (copied[2] !== 213'h33 || copied[3] !== 213'h44 ||
        copied_again[3].tag !== 7'b10xz101 ||
        copied_again[3].payload !== state[3].payload)
      $fatal(1, "force during aggregate copy");
    release copied;
    state[2].payload[1:0] = 2'bxz;
    #1;
    if (copied !== copied_again || copied[3].tag !== 7'b10xz101 ||
        copied[2] !== state[2])
      $fatal(1, "aggregate copy after release");
    copy_index = 80'h1_0000_0000_0000_0003;
    nested_index = 80'h1_0000_0000_0000_0000;
    #1;
    if (copied !== state || copied_again !== state)
      $fatal(1, "invalid indexed write before aggregate copy");
    if (nested_copy !== nested_state)
      $fatal(1, "invalid indexed write before nested copy");
    for (int repeat_index = 0; repeat_index < 3; ++repeat_index) begin
      for (int index = 1; index <= 4; ++index) check(index);
      check(-1);
      check('x);
      check('z);
      check(80'h1_0000_0000_0000_0002);
      for (int index = -2; index <= 1; ++index) check_nested(index);
      check_nested('x);
      check_nested('z);
      check_nested(80'h1_0000_0000_0000_0000);
    end
    $display("local aggregate stack passed");
    $finish;
  end
endmodule
// CHECK: local aggregate stack passed
