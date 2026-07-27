// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o0.native.out
// RUN: FileCheck %s --check-prefix=WARNING < %t.o0.native.err

class associative_item;
  int value;
endclass

module native_associative_array_types;
  typedef struct packed {
    logic [3:0] nibble;
    bit flag;
  } packed_t;

  logic [7:0] logic_values[int];
  real real_values[string];
  shortreal short_values[int];
  string string_values[int];
  packed_t packed_values[int];
  int dynamic_values[int][];
  int queue_values[int][$];
  int nested_values[int][string];
  int invalid_values[logic [3:0]];
  int unsigned_values[bit [3:0]];
  int wide_values[bit [63:0]];
  associative_item class_values[int];
  event first_event, second_event;
  event event_values[int];

  task automatic update_inout(inout int values[string]);
    values["inout"] = 22;
  endtask

  task automatic update_output(output int values[string]);
    values = '{"output": 33, default: 44};
  endtask

  initial begin
    int dynamic_source[];
    int queue_source[$];
    int copied_dynamic[int][];
    int inout_values[string];
    int output_values[string];
    logic [3:0] invalid_key;
    associative_item object;

    logic_values[-2] = 8'b10xz_0011;
    real_values["pi"] = 3.25;
    short_values[1] = 1.5;
    string_values[1] = "hello";
    packed_values[2] = '{nibble: 4'b1x01, flag: 1'b1};

    dynamic_source = '{1, 2};
    dynamic_values[3] = dynamic_source;
    queue_source = dynamic_source.find(item) with (item > 0);
    dynamic_source[0] = 9;
    copied_dynamic = dynamic_values;
    copied_dynamic[3][1] = 8;

    queue_values[4] = queue_source;
    queue_source[0] = 9;

    nested_values[2]["b"] = 20;
    nested_values[1]["a"] = 10;
    object = new;
    object.value = 7;
    class_values[1] = object;
    event_values[1] = first_event;
    event_values[2] = second_event;

    inout_values["before"] = 11;
    update_inout(inout_values);
    update_output(output_values);

    invalid_key = 4'b1x00;
    invalid_values[invalid_key] = 99;
    unsigned_values[4'hf] = 15;
    unsigned_values[4'h1] = 1;
    unsigned_values[4'h8] = 8;
    wide_values[64'hffff_ffff_ffff_ffff] = 2;
    wide_values[64'h0] = 1;
    $display("invalid read=%0d exists=%0d size=%0d",
             invalid_values[invalid_key],
             invalid_values.exists(invalid_key), invalid_values.size());
    $display("logic=%p real=%p short=%p string=%p packed=%p",
             logic_values, real_values, short_values, string_values,
             packed_values);
    $display("dynamic=%p copied=%p queue=%p nested=%p",
             dynamic_values, copied_dynamic, queue_values, nested_values);
    $display("inout=%p output=%p missing=%0d",
             inout_values, output_values, output_values["missing"]);
    $display("handles class=%0d event=%0d/%0d default=%0d",
             class_values[1].value, event_values[1] == first_event,
             event_values[2] == second_event, event_values[9] == null);
    $display("unsigned=%p wide=%p", unsigned_values, wide_values);
  end

  // CHECK: invalid read=0 exists=0 size=0
  // CHECK-NEXT: logic='{-2:
  // CHECK-SAME: real='{"pi":3.25}
  // CHECK-SAME: short='{1:1.5}
  // CHECK-SAME: string='{1:"hello"}
  // CHECK-NEXT: dynamic='{3:'{
  // CHECK-SAME: copied='{3:'{
  // CHECK-SAME: queue='{4:'{
  // CHECK-SAME: nested='{1:'{"a":
  // CHECK-SAME: 2:'{"b":
  // CHECK-NEXT: inout='{"before":
  // CHECK-SAME: "inout":
  // CHECK-SAME: output='{"output":
  // CHECK-SAME: missing=44
  // CHECK-NEXT: handles class=7 event=1/1 default=1
  // CHECK-NEXT: unsigned='{1:
  // CHECK-SAME: 8:
  // CHECK-SAME: 15:
  // CHECK-SAME: wide='{0:
  // CHECK-SAME: 18446744073709551615:
  // WARNING: warning: associative array operation ignored because the key contains X or Z bits
endmodule
