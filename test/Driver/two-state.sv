// RUN: talos --two-state -O0 --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 +trace=%t.o0.vcd | FileCheck %s
// RUN: talos --two-state -O3 %s -o %t.o3
// RUN: %t.o3 +trace=%t.o3.vcd | FileCheck %s && FileCheck %s --check-prefix=VCD < %t.o3.vcd
// RUN: talos --two-state --no-two-state-specialization -O3 %s -o %t.no-inference
// RUN: %t.no-inference | FileCheck %s
// RUN: talos --two-state --execution-tier=bytecode -O3 %s -o %t.bytecode
// RUN: %t.bytecode +trace=%t.bytecode.vcd | FileCheck %s

// Import changes the domain before runtime operators are lowered, including
// recursively nested arrays, records, enum bases and time. Cached elaboration
// results must not select runtime branches using four-state arithmetic.
// CHECK: two-state semantics passed
// The trace shows projected values and never an X/Z digit.
// VCD: $var reg 8 {{.}} a [7:0] $end
// VCD: $enddefinitions $end
// VCD: b101001 {{.}}
// VCD-NOT: {{[xz]}}
module top;
  string trace_file;
  localparam N = 3;
  localparam logic [7:0] PARAM_XZ = 8'bxz101z01;
  localparam logic [7:0] PATTERN = 8'b0010_????;
  typedef struct packed { logic [7:0] a; logic [8:0] b; } packet_t;
  typedef enum logic [1:0] { E0, E1, E2 } choice_t;
  logic clk, uninitialized;
  logic [7:0] a, b, q;
  logic signed [63:0] dividend, divisor;
  logic [128:0] wide;
  logic [7:0] mem[2:-1];
  logic [7:0] repeated[2];
  int keyed[4], dynamic_keyed[], queue_keyed[$];
  packet_t packet;
  choice_t choice;
  time timestamp;
  integer old_integer;
  int edges, branch, side_effects, index;
  wire [7:0] sum = a + b;
  wire gate;
  and (gate, a[0], b[0]);

  function automatic logic side_effect();
    side_effects++;
    return 1;
  endfunction

  always #1 clk = ~clk;
  always @(posedge clk) begin
    q <= sum;
    edges <= edges + 1;
  end

  initial begin
    if ($value$plusargs("trace=%s", trace_file)) begin
      $dumpfile(trace_file);
      $dumpvars(0, top);
    end
    if (uninitialized !== 0 || old_integer !== 0 || timestamp !== 0 ||
        packet !== 0 || choice !== E0 || mem[0] !== 0)
      $fatal(1, "two-state initialization");
    a = 8'bxz101z01;
    b = 3;
    if (a !== 8'h29 || PARAM_XZ !== 8'h29 || $isunknown(a))
      $fatal(1, "X/Z projection");
    if ((8'bx + 1) == 1) branch++; else $fatal(1, "cached condition");
    if (8'bz + 1 !== 1 || ~8'bz !== 8'hff || ('x ? 3 : 7) !== 7)
      $fatal(1, "project before computation");
    if ((1'bx && side_effect()) || (1'bz ? side_effect() : 0) || side_effects)
      $fatal(1, "short circuit");
    if (!(a ==? 8'b0010x001) || a !=? 8'b0010?001 ||
        !(a inside {8'b0010????}))
      $fatal(1, "wildcard comparison");
    casez (a)
      8'b0010????: branch++;
      default: $fatal(1, "casez pattern");
    endcase
    casex (a)
      8'b0010xxxx: branch++;
      default: $fatal(1, "casex pattern");
    endcase
    if (branch != 3) $fatal(1, "branch count");
    casez (a)
      PATTERN: branch++;
      default: $fatal(1, "parameter casez pattern");
    endcase
    casez (a)
      8'b0010_1xx1: $fatal(1, "casez x item never matches");
      default: branch++;
    endcase
    if (!(a inside {PATTERN}) || branch != 5)
      $fatal(1, "parameter inside pattern");
    wide = '1;
    if (wide + 129'd1 !== 0 || (wide >> 128) !== 1 || (wide << 129) !== 0)
      $fatal(1, "wide arithmetic and shifts");
    dividend = 64'sh8000000000000000;
    divisor = -1;
    if (dividend / divisor !== dividend || dividend % divisor !== 0)
      $fatal(1, "signed overflow");
    divisor = 0;
    if (dividend / divisor !== 0 || dividend % divisor !== 0)
      $fatal(1, "zero divisor");
    divisor = 65;
    if ((dividend >>> divisor) !== -64'sd1 || (dividend >> divisor) !== 0)
      $fatal(1, "oversized shift");
    mem[0] = a;
    if (mem[1'bx] !== a || a[1'bx] !== 1)
      $fatal(1, "projected selection index");
    index = 9;
    if (mem[index] !== 0 || a[index] !== 0)
      $fatal(1, "out-of-range read");
    mem[index] = 255;
    repeated = '{(N-1){8'bz + 1}};
    if (repeated[0] !== 1 || repeated[1] !== 1)
      $fatal(1, "replicated assignment pattern");
    keyed = '{(N-1): 7, default: 0};
    dynamic_keyed = '{(N-1): 9, default: 0};
    queue_keyed = '{(1+1): 11, default: 0};
    if (keyed[2] != 7 || keyed[1] != 0 || dynamic_keyed.size() != 3 ||
        dynamic_keyed[2] != 9 || queue_keyed.size() != 3 || queue_keyed[2] != 11)
      $fatal(1, "constant assignment-pattern keys");
    packet = '{a: {N-1{4'ha}}, b: 9'h155};
    if (packet.a[N+4:0] !== 8'haa || packet.b !== 9'h155)
      $fatal(1, "aggregate or elaborated bounds");
    #4;
    if (q !== 8'h2c || sum !== 8'h2c || gate !== 1 || edges != 2)
      $fatal(1, "two-state edge/NBA scheduling");
    force gate = 1'bx;
    #2;
    if (gate !== 0) $fatal(1, "net force projection");
    release gate;
    force a = 8'h55;
    #2;
    if (a !== 8'h55 || sum !== 8'h58 || gate !== 1) $fatal(1, "force");
    release a;
    a = 7;
    #2;
    if (a !== 7 || sum !== 10 || q !== 10) $fatal(1, "release");
    if ($past(q, 8, , @(posedge clk)) !== 0)
      $fatal(1, "two-state sampled history default");
    $display("two-state semantics passed");
    $finish;
  end
endmodule
