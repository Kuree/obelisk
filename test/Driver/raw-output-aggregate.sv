// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s --check-prefix=SIM
// RUN: obelisk -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.data | FileCheck %s
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.data | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.data | FileCheck %s

module raw_output_aggregate;
  typedef struct {
    byte first;
    logic [3:0] second;
  } pair_t;
  typedef struct {
    logic [5:0] head;
    pair_t tail;
  } nested_t;
  typedef union {
    logic [7:0] first;
    logic [31:0] wider;
  } union_t;
  typedef struct {
    logic [31:0] a;
    logic [31:0] b;
  } ascii_t;
  typedef struct {
    logic [3:0] prefix;
    union_t choice;
    byte suffix;
  } nested_union_t;

  pair_t pair_value;
  nested_t nested_value;
  union_t union_value;
  ascii_t ascii_value;
  nested_union_t nested_union_value;
  string expected;
  string actual;
  string dynamic_format;
  string task_result;
  string output_path;
  integer descriptor;
  integer status;
  pair_t pair_read;
  nested_t nested_read;
  union_t union_read;

  initial begin
    pair_value.first = 8'ha5;
    pair_value.second = 4'bxz10;
    nested_value.head = 6'b10z1x0;
    nested_value.tail = pair_value;
    union_value.first = 8'b10xz01z1;
    nested_union_value.prefix = 4'b1xz0;
    nested_union_value.choice = union_value;
    nested_union_value.suffix = 8'h3c;

    // Each leaf is independently padded to a native 32-bit word. Comparing
    // against separately formatted leaves prevents an aggregate-wide packed
    // concatenation from accidentally satisfying the round trip.
    expected = $sformatf("%z%z", pair_value.first, pair_value.second);
    actual = $sformatf("%z", pair_value);
    $display("pair-z=%0d:%0d", actual == expected, actual.len());

    expected = $sformatf("%z%z%z", nested_value.head,
                         nested_value.tail.first,
                         nested_value.tail.second);
    dynamic_format = "%Z";
    actual = $sformatf(dynamic_format, nested_value);
    $display("nested-z=%0d:%0d", actual == expected, actual.len());

    expected = $sformatf("%u%u", pair_value.first, pair_value.second);
    actual = $sformatf("%U", pair_value);
    $display("pair-u=%0d:%0d", actual == expected, actual.len());

    $sformat(task_result, "%z", union_value);
    expected = $sformatf("%z", union_value.first);
    $display("union-z=%0d:%0d", task_result == expected, task_result.len());

    expected = $sformatf("%z%z%z", nested_union_value.prefix,
                         nested_union_value.choice.first,
                         nested_union_value.suffix);
    actual = $sformatf("%z", nested_union_value);
    $display("nested-union=%0d:%0d", actual == expected, actual.len());

    expected = $sformatf("%p", pair_value);
    dynamic_format = "%p";
    actual = $sformatf(dynamic_format, pair_value);
    $display("dynamic-p=%0d", actual == expected);

    // The literal "%z" is consumed by %s and must not be reinterpreted as a
    // new format. Once those conversions end, the surplus aggregate is an
    // ordinary pattern item.
    expected = {"raw_output_aggregate ", $sformatf("%u", pair_value),
                " %z", $sformatf("", pair_value)};
    actual = $sformatf("%m %u %s", pair_value, "%z", pair_value);
    $display("consumed-format=%0d", actual == expected);

    expected = $sformatf("%% %m %l %u%u", pair_value.first,
                         pair_value.second);
    actual = $sformatf("%% %m %l %u", pair_value);
    $display("nonconsuming=%0d", actual == expected);

    expected = $sformatf("%08.2f %z%z", 1.25, pair_value.first,
                         pair_value.second);
    actual = $sformatf("%08.2f %z", 1.25, pair_value);
    $display("options=%0d", actual == expected);

    if (!$value$plusargs("OUT=%s", output_path))
      $fatal(0, "missing output path");
    descriptor = $fopen(output_path, "w");
    // Each literal is a new format after the preceding conversion is
    // exhausted; the specialization tracker must restart at each boundary.
    $fwrite(descriptor, "%z", pair_value, "%u", nested_value, "%z",
            union_value);
    $fclose(descriptor);
    descriptor = $fopen(output_path, "r");
    status = $fscanf(descriptor, "%z%u%z", pair_read, nested_read,
                     union_read);
    $fclose(descriptor);
    $display("file=%0d:%0d:%0d:%0d", status,
             pair_read === pair_value,
             nested_read.head === 6'b100100 &&
                 nested_read.tail.first === 8'ha5 &&
                 nested_read.tail.second === 4'b0010,
             union_read.first === union_value.first);

    // These known 32-bit leaves make the raw bytes themselves printable, so
    // both ordinary output tasks exercise the same logical aggregate path.
    ascii_value.a = 32'h44434241;
    ascii_value.b = 32'h48474645;
    $display("%u", ascii_value);
    $write("%u\n", ascii_value);
  end
endmodule

// A logical aggregate item owns three physical managed strings: its existing
// pattern rendering plus the two leaf-padded raw encodings.
// SIM: obelisk_sim.string.output_format
// SIM: obelisk_sim.display{{.*}}flags = [0, 4096]

// CHECK: pair-z=1:16
// CHECK-NEXT: nested-z=1:24
// CHECK-NEXT: pair-u=1:8
// CHECK-NEXT: union-z=1:8
// CHECK-NEXT: nested-union=1:24
// CHECK-NEXT: dynamic-p=1
// CHECK-NEXT: consumed-format=1
// CHECK-NEXT: nonconsuming=1
// CHECK-NEXT: options=1
// CHECK-NEXT: file=3:1:1:1
// CHECK-NEXT: ABCDEFGH
// CHECK-NEXT: ABCDEFGH
