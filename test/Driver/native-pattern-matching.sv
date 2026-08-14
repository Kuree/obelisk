// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out
// RUN: test ! -s %t.o3.native.err

module native_pattern_matching;
  typedef struct packed {
    logic [3:0] left;
    logic [3:0] right;
  } pair_t;
  typedef struct packed {
    pair_t inner;
    logic enabled;
  } nested_t;
  typedef union tagged packed {
    logic [7:0] Byte;
    pair_t Pair;
  } tagged_t;
  typedef union tagged {
    void Invalid;
    logic [7:0] Valid;
  } status_t;
  typedef union tagged packed {
    logic [7:0] Only;
  } single_t;

  pair_t pair;
  nested_t nested;
  tagged_t tagged_value;
  status_t status;
  single_t single;
  int filter_calls;
  int iteration;

  function automatic logic accept(input logic [3:0] value);
    filter_calls++;
    return value == 4'd1;
  endfunction

  initial begin
    pair = {4'd1, 4'd2};
    nested = {pair, 1'b1};

    case (pair) matches
      '{left: .capture, right: 4'd2} &&& capture == 4'd1:
        $display("named=%0d", capture);
      default: $display("named=default");
    endcase

    case (pair) matches
      '{.first, .second} &&& first + second == 4'd3:
        $display("ordered=%0d,%0d", first, second);
      default: $display("ordered=default");
    endcase

    case (nested) matches
      '{inner: '{left: .deep, right: 4'd2}, enabled: 1'b1}:
        $display("nested=%0d", deep);
      default: $display("nested=default");
    endcase

    filter_calls = 0;
    case (pair) matches
      '{left: 4'd0, right: .*} &&& accept(4'd0):
        $display("filter=bad-first");
      '{left: .filtered, right: .*} &&& accept(filtered):
        $display("filter=%0d:%0d", filtered, filter_calls);
      default: $display("filter=default");
    endcase

    if (pair matches '{left: .x, right: .*} &&& x == 4'd1 &&&
        pair matches '{left: .*, right: .y} &&& y == 4'd2)
      $display("chained=%0d,%0d", x, y);
    else
      $display("chained=default");

    unique0 if (pair matches '{left: 4'd0, right: .*} &&&
                pair matches '{left: .*, right: .late})
      $display("unique-pattern=first:%0d", late);
    else if (pair matches '{left: .chosen, right: .*})
      $display("unique-pattern=second:%0d", chosen);

    if (pair matches '{left: .shadow, right: .*})
      $display("shadow-left=%0d", shadow);
    if (pair matches '{left: .*, right: .shadow})
      $display("shadow-right=%0d", shadow);

    tagged_value = 'x;
    case (tagged_value) matches
      tagged Byte .*: $display("tagged-unknown=byte");
      tagged Pair .*: $display("tagged-unknown=pair");
      default: $display("tagged-unknown=default");
    endcase

    tagged_value = tagged Pair pair;
    case (tagged_value) matches
      tagged Byte .byte_capture: $display("tagged=byte:%0d", byte_capture);
      tagged Pair '{left: .tag_left, right: .tag_right}:
        $display("tagged=pair:%0d,%0d", tag_left, tag_right);
      default: $display("tagged=default");
    endcase

    status = tagged Invalid;
    case (status) matches
      tagged Valid .valid_value: $display("status=valid:%0d", valid_value);
      tagged Invalid: $display("status=invalid");
      default: $display("status=default");
    endcase

    single = 'x;
    case (single) matches
      tagged Only .*: $display("single-tag=active");
      default: $display("single-tag=default");
    endcase

    iteration = 0;
    repeat (2) begin
      if (iteration == 0)
        pair = {4'd1, 4'd0};
      else
        pair = {4'd2, 4'd0};
      if (pair matches '{left: .fork_snapshot, right: .*})
        fork
          begin
            #(3 - fork_snapshot);
            $display("fork-snapshot=%0d", fork_snapshot);
          end
        join_none
      iteration++;
    end
    #4;

    if (pair matches '{left: .fork_capture, right: .*})
      fork
        begin
          #1;
          $display("fork-capture=%0d", fork_capture);
        end
      join
  end
endmodule

// STDOUT: named=1
// STDOUT-NEXT: ordered=1,2
// STDOUT-NEXT: nested=1
// STDOUT-NEXT: filter=1:1
// STDOUT-NEXT: chained=1,2
// STDOUT-NEXT: unique-pattern=second:1
// STDOUT-NEXT: shadow-left=1
// STDOUT-NEXT: shadow-right=2
// STDOUT-NEXT: tagged-unknown=default
// STDOUT-NEXT: tagged=pair:1,2
// STDOUT-NEXT: status=invalid
// STDOUT-NEXT: single-tag=active
// STDOUT-NEXT: fork-snapshot=2
// STDOUT-NEXT: fork-snapshot=1
// STDOUT-NEXT: fork-capture=2
