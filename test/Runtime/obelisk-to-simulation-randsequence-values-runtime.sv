// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s

// IEEE 1800-2023 18.17.6 and 18.17.7: production values are local to
// their generating rule; arrays use [1:N] in syntactic order. Repeat updates
// one occurrence; conditional and case branches retain their own slots.
// Test default and empty inputs, strings, 129-bit and unpacked-array returns,
// early return, input copies, output/inout copy-back, ref aliasing, captured
// output indices, grammar break inside an outer loop, and nested grammars.
// CHECK: inside ports 1 1 1
// CHECK-NEXT: production values 1 1 1 1 1 1 1 1 1 1 1
// CHECK-NEXT: production ports 1 1 1 1 1
// CHECK-NEXT: captured output address 1 1 1
// CHECK-NEXT: default argument bindings 1 1 1 1
// CHECK-NEXT: managed ref argument 1
// CHECK-NEXT: grammar abort 1 1 1
// CHECK-NEXT: nested grammar 1
// CHECK-NEXT: typed root 1

typedef int pair_t[1:2];

class C;
  bit [3:0] v = 2;
endclass

module top;
  int in_v = 7;
  int out_v = -1;
  int io_v = 10;
  int alias_v = 2;
  int count = 0;
  int index = 0;
  int iterations = 0;
  int default_out = 0;
  int default_io = 10;
  int default_alias = 20;
  int default_count = 0;
  int outarray[2];
  pair_t pairs;
  bit ran = 0;
  C object = new;

  initial begin
    pairs[1] = 10;
    pairs[2] = 11;
    randsequence (main)
      main : value(7) value() value repeat (3) counter B
             if (1) pick(5) else pick(20)
             case (2)
               1 : pick(30);
               2 : pick(40);
             endcase
             echo wide ports(in_v, out_v, io_v, alias_v)
             output_leaf(outarray[index++]) pair_p defaults
             defaults(default_out, default_io, default_alias, 9)
             managed_ref(object.v)
             {
               pairs[1] = 12;
               $display("production values %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d",
                        value[1] == 7, value[2] == 11, value[3] == 11,
                        counter == 3, B == 5, pick[1] == 5, pick[4] == 40,
                        echo == "done",
                        wide == 129'd1267650600228229401496703205381,
                        pair_p[1] == 10, pair_p[2] == 11);
               $display("production ports %0d %0d %0d %0d %0d", in_v == 7,
                        out_v == 100, io_v == 13, alias_v == 6, count == 5);
               $display("captured output address %0d %0d %0d", index == 1,
                        outarray[0] == 9, outarray[1] == 0);
               $display("default argument bindings %0d %0d %0d %0d",
                        default_out == 9, default_io == 12,
                        default_alias == 24, default_count == 1);
               $display("managed ref argument %0d", object.v == 15);
             }
             ;
      int value(int p = 11) : { return p; count += 1000; };
      int counter : { count = count + 1; return count; };
      int B : counter { return counter; } := 0
            | counter counter { return counter[2]; } := 1;
      int pick(int p) : { return p; };
      string echo(string s = "done") : { return "bad"; } := 0
                                     | { return s; } := 1;
      bit [128:0] wide : { return 129'd1267650600228229401496703205381; };
      pair_t pair_p : { return pairs; };
      void ports(int p, output int o, inout int io, ref int ali)
        : { p = 100;
            o = p;
            io = io + 3;
            ali = ali + 4;
            $display("inside ports %0d %0d %0d", out_v == -1, io_v == 10,
                     alias_v == 6);
            return;
            count += 1000;
          };
      void output_leaf(output int o) : { o = 9; };
      void defaults(output int o = default_out, inout int io = default_io,
                    ref int ali = default_alias, input int p = default_count++)
        : { o = p; io = io + 1; ali = ali + 2; };
      void managed_ref(ref bit [3:0] v) : { v = 15; };
    endsequence

    repeat (2) begin
      randsequence (main)
        main : outer(out_v) { count += 1000; };
        void outer(output int o) : { o = 10; } inner(o) { count += 1000; };
        void inner(output int o) : { o = 200; break; count += 1000; };
      endsequence
      iterations = iterations + 1;
    end
    $display("grammar abort %0d %0d %0d", out_v == 200, iterations == 2,
             count == 5);

    randsequence (main)
      main : worker(out_v);
      void worker(output int o)
        : { o = 10;
            randsequence (main)
              main : { break; };
            endsequence
            o = 111;
            return;
          };
    endsequence
    $display("nested grammar %0d", out_v == 111);

    randsequence (main)
      bit [3:0] main : { ran = 1; return 9; };
    endsequence
    $display("typed root %0d", ran);
  end
endmodule
