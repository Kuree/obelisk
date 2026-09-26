// XFAIL: *
// Slang v11 does not accept qualified assignment-pattern type keys.
// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk -emit-slang %s | FileCheck %s --check-prefix=SLANG
// RUN: obelisk -emit-obelisk %s | FileCheck %s --check-prefix=OBELISK

package assignment_pattern_types;
  typedef enum logic [1:0] { RED, GREEN, BLUE } color_t;
endpackage

module native_assignment_pattern_setters;
  typedef struct packed { logic [3:0] high; logic [7:0] low; } word_t;
  typedef struct { int number; byte octet; } leaf_t;
  typedef struct { int head; leaf_t nested; shortint tail; } tree_t;
  logic [3:0] unpacked[3];
  logic [3:0] unknowns[4];
  logic [3:0] packed_bits;
  logic [7:0] replicated[4];
  word_t record;
  leaf_t leaf;
  tree_t tree;
  int words[3];
  int dynamic_values[];
  int queue_values[$];
  assignment_pattern_types::color_t colors[2];
  typedef struct {
    assignment_pattern_types::color_t color;
    int number;
  } qualified_t;
  qualified_t qualified;
  typedef struct { logic flag; int number; real fraction; } real_record_t;
  real_record_t real_record;
  real real_values[2];
  int real_index = 1;

  initial begin
    unpacked = '{default: 4'ha};
    unknowns = '{default: 4'bx01z};
    assert (unknowns[0] === 4'bx01z && unknowns[3] === 4'bx01z);
    packed_bits = '{default: 1'b1};
    replicated = '{2{8'h12, 8'h34}};
    record = '{default: '1};
    // IEEE 1800-2017 10.9.1 and 10.9.2: the last matching type key
    // wins, member keys take precedence, and type/default keys recurse into
    // fixed arrays and structures until they reach matching singular values.
    leaf = '{int: 1, int: 2, byte: 3};
    assert (leaf.number == 2 && leaf.octet == 3);
    tree = '{int: 10, shortint: 20, default: 8'hff};
    assert (tree.head == 10 && tree.nested.number == 10 &&
            tree.nested.octet == -1 && tree.tail == 20);
    tree = '{int: 11, shortint: 22,
             nested: '{number: 33, octet: 44}, default: 0};
    assert (tree.head == 11 && tree.nested.number == 33 &&
            tree.nested.octet == 44 && tree.tail == 22);
    words = '{int: 7};
    assert (words[0] == 7 && words[1] == 7 && words[2] == 7);
    // IEEE 1800-2017 10.9 grammar makes a ps_type_identifier a simple_type
    // even when it names an enum. Package-qualified type keys work in both
    // fixed arrays and structures, and take precedence over default.
    colors = '{default: '0,
               assignment_pattern_types::color_t:
                   assignment_pattern_types::GREEN};
    qualified = '{default: 9,
                   assignment_pattern_types::color_t:
                       assignment_pattern_types::BLUE};
    $display("qualified=%0d/%0d/%0d/%0d", colors[0], colors[1],
             qualified.color, qualified.number);
    assert (colors[0] == assignment_pattern_types::GREEN &&
            colors[1] == assignment_pattern_types::GREEN &&
            qualified.color == assignment_pattern_types::BLUE &&
            qualified.number == 9);
    // IEEE 1800-2017 7.2 and 7.4.2 permit real members and elements in
    // unpacked aggregates. Bytecode crosses aggregate storage with explicit
    // raw-bit casts, matching native representation and comparisons.
    real_record = '{1'bx, 12, 1.25};
    real_values = '{3.5, 4.75};
    assert (real_record.flag === 1'bx && real_record.number == 12 &&
            real_record.fraction == 1.25 &&
            real_values[real_index] == 4.75);
    dynamic_values = '{2: 9, default: 4};
    queue_values = '{2: 8, default: 5};
    assert (dynamic_values.size() == 3 && dynamic_values[0] == 4 &&
            dynamic_values[1] == 4 && dynamic_values[2] == 9);
    assert (queue_values.size() == 3 && queue_values[0] == 5 &&
            queue_values[1] == 5 && queue_values[2] == 8);
    dynamic_values = '{3{1, 2}};
    queue_values = '{2{4, 5}};
    assert (dynamic_values.size() == 6 && dynamic_values[0] == 1 &&
            dynamic_values[1] == 2 && dynamic_values[4] == 1 &&
            dynamic_values[5] == 2);
    assert (queue_values.size() == 4 && queue_values[0] == 4 &&
            queue_values[1] == 5 && queue_values[2] == 4 &&
            queue_values[3] == 5);
    $display("u=%h%h%h p=%h r=%h%h%h%h s=%h leaf=%0d/%0d tree=%0d/%0d/%0d/%0d words=%0d%0d%0d",
             unpacked[0], unpacked[1],
             unpacked[2], packed_bits, replicated[0], replicated[1], replicated[2],
             replicated[3], record, leaf.number, leaf.octet, tree.head,
             tree.nested.number, tree.nested.octet, tree.tail, words[0],
             words[1], words[2]);
    // CHECK: qualified=1/1/2/9
    // CHECK: u=aaa p=f r=12341234 s=fff leaf=2/3 tree=11/33/44/22 words=777
  end

  // SLANG: type_setter_count = 3 : i64, type_setter_types = [!slang.integral<32
  // SLANG-SAME: !slang.integral<32
  // SLANG-SAME: !slang.integral<8
  // OBELISK: type_setter_count = 3 : i64, type_setter_types = [!obelisk.integral<32
  // OBELISK-SAME: !obelisk.integral<32
  // OBELISK-SAME: !obelisk.integral<8
endmodule
