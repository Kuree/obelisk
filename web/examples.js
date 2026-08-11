export const EXAMPLES = [
  {
    name: 'Counter',
    source: `// A clocked counter. Obelisk compiles this ahead of time to WebAssembly.
module counter;
  logic clk = 0;
  int   count = 0;

  initial begin
    $dumpfile("counter.vcd");
    $dumpvars(0, counter);
  end

  always #5 clk = ~clk;

  always @(posedge clk) begin
    count <= count + 1;
    if (count == 10) begin
      $display("counted to %0d at time %0t", count, $time);
      $finish;
    end
  end
endmodule
`,
  },
  {
    name: 'LFSR',
    source: `// 32-bit maximal-length LFSR, a compute-bound loop.
module lfsr;
  logic clk = 0;
  logic [31:0] state = 32'hACE1_2345;
  int   cycles = 0;

  always #5 clk = ~clk;

  always @(posedge clk) begin
    state  <= {state[30:0], state[31] ^ state[21] ^ state[1] ^ state[0]};
    cycles <= cycles + 1;
    if (cycles == 100000) begin
      $display("after %0d cycles state = %08h", cycles, state);
      $finish;
    end
  end
endmodule
`,
  },
  {
    name: 'Classes',
    source: `// Classes, virtual dispatch and dynamic allocation.
module oop;
  class Shape;
    virtual function string name();
      return "shape";
    endfunction
  endclass

  class Circle extends Shape;
    virtual function string name();
      return "circle";
    endfunction
  endclass

  Shape shapes[2];

  initial begin
    shapes[0] = new();
    shapes[1] = Circle::new();
    foreach (shapes[i])
      $display("shapes[%0d] is a %s", i, shapes[i].name());
    $finish;
  end
endmodule
`,
  },
  {
    name: 'Randomization',
    source: `// Constrained random generation.
module rnd;
  class Packet;
    rand bit [7:0] length;
    rand bit [3:0] kind;
    constraint c_length { length inside {[16:64]}; }
    constraint c_kind   { kind < 4; }
  endclass

  initial begin
    Packet p = new();
    for (int i = 0; i < 5; i++) begin
      if (!p.randomize())
        $display("randomize failed");
      else
        $display("packet %0d: length=%0d kind=%0d", i, p.length, p.kind);
    end
    $finish;
  end
endmodule
`,
  },
];
