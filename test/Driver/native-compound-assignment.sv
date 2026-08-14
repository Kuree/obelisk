// RUN: obelisk -fno-lto --std=1800-2023 -Wno-nonstandard-string-concat -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: FileCheck %s < %t.o0.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -Wno-nonstandard-string-concat -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -fno-lto --std=1800-2023 -Wno-nonstandard-string-concat -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: obelisk -fno-lto --std=1800-2023 -Wno-nonstandard-string-concat -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out

class compound_box;
  int value;
endclass

module native_compound_assignment;
  int index_calls;
  int selected_index;
  int queue_values[$];
  event event_control;
  event repeat_control;

  function automatic int next_index();
    index_calls++;
    return selected_index;
  endfunction

  task automatic add_ref(ref int value);
    value += 9;
  endtask

  initial begin
    int unsigned arithmetic;
    int unsigned bits;
    int signed signed_bits;
    logic [7:0] four_state;
    real real_value;
    string text;
    int expression_result;
    int i;
    int j;
    int fixed_values [0:1];
    logic [15:0] packed_value;
    logic [7:0] high;
    logic [7:0] low;
    int dynamic_values[];
    int timed_values [0:1];
    int timed_index;
    int rhs;
    int event_value;
    int repeat_value;
    int ref_value;
    compound_box box;
    compound_box original_box;

    arithmetic = 20;
    arithmetic += 5;
    arithmetic -= 3;
    arithmetic *= 2;
    arithmetic /= 4;
    arithmetic %= 6;
    $display("arithmetic=%0d", arithmetic);

    bits = 'h35;
    bits &= 'h0f;
    bits |= 'h20;
    bits ^= 'h05;
    bits <<= 2;
    bits >>= 1;
    bits <<<= 1;
    $display("bits=%0h", bits);

    signed_bits = -64;
    signed_bits >>>= 2;
    $display("signed_shift=%0d", signed_bits);

    four_state = 8'b10x1_0011;
    four_state &= 8'b1111_0111;
    four_state |= 8'b0000_1000;
    four_state ^= 8'b0010_0001;
    $display("four_state=%b", four_state);

    real_value = 1.5;
    real_value += 2.25;
    real_value *= 2.0;
    real_value /= 3.0;
    real_value -= 0.5;
    $display("real=%.2f", real_value);

    text = "ab";
    text += "cd";
    $display("string=%s", text);

    arithmetic = 10;
    expression_result = (arithmetic += 7);
    $display("expression=%0d/%0d", expression_result, arithmetic);

    i = 0;
    j = 0;
    for (; i < 3; i++, j += 2)
      ;
    $display("for=%0d/%0d", i, j);

    fixed_values[0] = 10;
    fixed_values[1] = 20;
    index_calls = 0;
    selected_index = 1;
    expression_result = (fixed_values[next_index()] += 5);
    $display("fixed=%0d/%0d/%0d", fixed_values[1], expression_result,
             index_calls);

    packed_value = 16'h1234;
    selected_index = 4;
    index_calls = 0;
    packed_value[next_index() +: 4] += 3;
    $display("packed=%h/%0d", packed_value, index_calls);

    high = 8'h01;
    low = 8'h02;
    {high, low} += 16'h0102;
    $display("concat=%h/%h", high, low);

    text = "AZ";
    selected_index = 1;
    index_calls = 0;
    text[next_index()] += 1;
    $display("character=%s/%0d", text, index_calls);

    dynamic_values = new[2];
    dynamic_values[1] = 30;
    selected_index = 1;
    index_calls = 0;
    dynamic_values[next_index()] += 4;
    $display("dynamic=%0d/%0d", dynamic_values[1], index_calls);

    dynamic_values[0] = 1;
    dynamic_values[1] = 2;
    {dynamic_values[0], dynamic_values[1]} += 64'h00000001_00000002;
    $display("dynamic_concat=%0d/%0d", dynamic_values[0],
             dynamic_values[1]);

    dynamic_values[0] = 40;
    dynamic_values[1] = 50;
    queue_values = dynamic_values.find(item) with (item >= 0);
    selected_index = 0;
    index_calls = 0;
    queue_values[next_index()] *= 2;
    $display("queue=%0d/%0d", queue_values[0], index_calls);

    queue_values[0] = 1;
    queue_values[1] = 2;
    {queue_values[0], queue_values[1]} += 64'h00000001_00000002;
    $display("queue_concat=%0d/%0d", queue_values[0], queue_values[1]);

    ref_value = 4;
    add_ref(ref_value);
    box = new;
    box.value = 5;
    box.value += 7;
    $display("references=%0d/%0d", ref_value, box.value);

    timed_values[0] = 10;
    timed_values[1] = 20;
    timed_index = 0;
    rhs = 3;
    fork
      begin
        timed_values[timed_index++] += #5 rhs;
      end
      begin
        #1;
        timed_values[0] = 100;
        timed_values[1] = 200;
        rhs = 30;
      end
    join
    $display("delay=%0d/%0d/%0d/%0d", timed_values[0], timed_values[1],
             timed_index, rhs);

    dynamic_values[0] = 10;
    dynamic_values[1] = 20;
    selected_index = 0;
    index_calls = 0;
    rhs = 3;
    fork
      begin
        dynamic_values[next_index()] += #5 rhs;
      end
      begin
        #1;
        dynamic_values[0] = 100;
        selected_index = 1;
        rhs = 30;
      end
    join
    $display("container_delay=%0d/%0d/%0d/%0d", dynamic_values[0],
             dynamic_values[1], index_calls, selected_index);

    text = "AZ";
    selected_index = 1;
    index_calls = 0;
    rhs = 1;
    fork
      begin
        text[next_index()] += #5 rhs;
      end
      begin
        #1;
        text = "QQ";
        selected_index = 0;
        rhs = 10;
      end
    join
    $display("character_delay=%s/%0d/%0d", text, index_calls,
             selected_index);

    high = 8'h01;
    low = 8'h02;
    fork
      begin
        {high, low} += #5 16'h0102;
      end
      begin
        #1;
        high = 8'haa;
        low = 8'hbb;
      end
    join
    $display("concat_delay=%h/%h", high, low);

    box.value = 10;
    original_box = box;
    rhs = 3;
    fork
      begin
        box.value += #5 rhs;
      end
      begin
        #1;
        box = new;
        box.value = 100;
        rhs = 30;
      end
    join
    $display("managed_delay=%0d/%0d/%0d", original_box.value, box.value, rhs);

    event_value = 10;
    rhs = 3;
    fork
      begin
        event_value += @(event_control) rhs;
      end
      begin
        #1;
        event_value = 100;
        rhs = 30;
        ->event_control;
      end
    join
    $display("event=%0d/%0d", event_value, rhs);

    repeat_value = 10;
    rhs = 3;
    fork
      begin
        repeat_value += repeat (2) @(repeat_control) rhs;
      end
      begin
        #1;
        repeat_value = 100;
        rhs = 30;
        ->repeat_control;
        #1;
        repeat_value = 200;
        rhs = 40;
        ->repeat_control;
      end
    join
    $display("repeat=%0d/%0d", repeat_value, rhs);
  end
endmodule

// CHECK: arithmetic=5
// CHECK: bits=80
// CHECK: signed_shift=-16
// CHECK: four_state=10x11010
// CHECK: real=2.00
// CHECK: string=abcd
// CHECK: expression=17/17
// CHECK: for=3/6
// CHECK: fixed=25/25/1
// CHECK: packed=1264/1
// CHECK: concat=02/04
// CHECK: character=A[/1
// CHECK: dynamic=34/1
// CHECK: dynamic_concat=2/4
// CHECK: queue=80/1
// CHECK: queue_concat=2/4
// CHECK: references=13/12
// CHECK: delay=13/200/1/30
// CHECK: container_delay=13/20/1/1
// CHECK: character_delay=Q[/1/0
// CHECK: concat_delay=02/04
// CHECK: managed_delay=13/100/30
// CHECK: event=13/30
// CHECK: repeat=13/40
