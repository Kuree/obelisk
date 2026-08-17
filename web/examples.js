export const EXAMPLES = [
  {
    name: 'Counter',
    source: `// Nonblocking assignments update this counter after each rising edge.
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
    source: `// A 32-bit Fibonacci LFSR with taps at bits 31, 21, 1, and 0.
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
    name: 'SystemVerilog assertions',
    source: `// A temporal property checks a multi-cycle request/response protocol.
module sva_protocol;
  logic clk = 0;
  logic rst = 1;
  logic req = 0;
  logic grant = 0;
  logic busy = 0;
  logic done = 0;
  logic error = 0;
  int completed = 0;

  // A response grants the request, remains busy for two clocks, then
  // completes without an error.
  sequence response_path;
    grant ##1 busy[*2] ##1 (done && !error);
  endsequence

  // |=> starts the response sequence on the clock after req. Reset can
  // asynchronously cancel any live attempt.
  property bounded_response;
    @(posedge clk) disable iff (rst)
      req |=> response_path;
  endproperty

  response_check: assert property (bounded_response)
    else $error("request did not receive a bounded response");
  response_coverage: cover property (bounded_response) completed++;

  always #1 clk = ~clk;

  initial begin
    $dumpfile("sva-protocol.vcd");
    $dumpvars(0, sva_protocol);

    // A complete response.
    @(negedge clk); rst = 0; req = 1;
    @(negedge clk); req = 0; grant = 1;
    @(negedge clk); grant = 0; busy = 1;
    @(negedge clk);
    @(negedge clk); busy = 0; done = 1;
    @(negedge clk); done = 0; req = 1;

    // Cancel the next attempt while its response is in progress.
    @(negedge clk); req = 0; grant = 1;
    @(negedge clk); grant = 0; rst = 1; busy = 1;

    // Reset deassertion permits a fresh successful attempt.
    @(negedge clk); rst = 0; busy = 0; req = 1;
    @(negedge clk); req = 0; grant = 1;
    @(negedge clk); grant = 0; busy = 1;
    @(negedge clk);
    @(negedge clk); busy = 0; done = 1;
    @(negedge clk); done = 0;

    $display("completed assertion attempts: %0d", completed);
    $finish;
  end
endmodule
`,
  },
  {
    name: 'RV32IM core',
    source: `// A single-cycle RV32IM core with separate instruction and data ports.
module rv32im_core (
  input  logic        clk,
  input  logic        reset,
  input  logic [31:0] instruction,
  input  logic [31:0] data_rdata,
  output logic [31:0] pc,
  output logic [31:0] data_addr,
  output logic [31:0] data_wdata,
  output logic [3:0]  data_wstrb,
  output logic        halted,
  output logic [31:0] result
);
  logic [31:0] x[0:31];
  wire [4:0] rd  = instruction[11:7];
  wire [4:0] rs1 = instruction[19:15];
  wire [4:0] rs2 = instruction[24:20];
  wire [2:0] funct3 = instruction[14:12];
  wire [6:0] funct7 = instruction[31:25];

  wire [31:0] imm_i = {{20{instruction[31]}}, instruction[31:20]};
  wire [31:0] imm_s = {{20{instruction[31]}}, instruction[31:25],
                       instruction[11:7]};
  wire [31:0] imm_b = {{19{instruction[31]}}, instruction[31],
                       instruction[7], instruction[30:25],
                       instruction[11:8], 1'b0};
  wire [31:0] imm_u = {instruction[31:12], 12'b0};
  wire [31:0] imm_j = {{11{instruction[31]}}, instruction[31],
                       instruction[19:12], instruction[20],
                       instruction[30:21], 1'b0};

  wire [31:0] load_word = data_rdata >> (8 * data_addr[1:0]);
  wire signed [63:0] product_ss =
    $signed({{32{x[rs1][31]}}, x[rs1]})
      * $signed({{32{x[rs2][31]}}, x[rs2]});
  wire [63:0] product_uu = {32'b0, x[rs1]} * {32'b0, x[rs2]};
  wire signed [64:0] product_su =
    $signed({{33{x[rs1][31]}}, x[rs1]})
      * $signed({33'b0, x[rs2]});
  integer i;

  assign result = x[6];

  // Byte strobes allow the same data port to implement SB, SH, and SW.
  always_comb begin
    data_addr  = x[rs1] + imm_i;
    data_wdata = x[rs2];
    data_wstrb = 4'b0000;
    if (instruction[6:0] == 7'b0100011) begin
      data_addr = x[rs1] + imm_s;
      case (funct3)
        3'b000: begin
          data_wdata = {4{x[rs2][7:0]}};
          data_wstrb = 4'b0001 << data_addr[1:0];
        end
        3'b001: begin
          data_wdata = {2{x[rs2][15:0]}};
          data_wstrb = 4'b0011 << data_addr[1:0];
        end
        3'b010: data_wstrb = 4'b1111;
        default: data_wstrb = 4'b0000;
      endcase
    end
  end

  always @(posedge clk) begin
    if (reset) begin
      pc     <= 0;
      halted <= 0;
      for (i = 0; i < 32; i = i + 1)
        x[i] <= 0;
    end else if (!halted) begin
      x[0] <= 0;
      pc   <= pc + 4;

      case (instruction[6:0])
        7'b0110111: if (rd != 0) x[rd] <= imm_u;      // LUI
        7'b0010111: if (rd != 0) x[rd] <= pc + imm_u; // AUIPC

        7'b1101111: begin // JAL
          if (rd != 0) x[rd] <= pc + 4;
          pc <= pc + imm_j;
        end
        7'b1100111: begin // JALR
          if (funct3 == 3'b000) begin
            if (rd != 0) x[rd] <= pc + 4;
            pc <= (x[rs1] + imm_i) & 32'hffff_fffe;
          end else begin
            halted <= 1;
          end
        end

        7'b1100011: begin // Conditional branches
          case (funct3)
            3'b000: if (x[rs1] == x[rs2])
                      pc <= pc + imm_b; // BEQ
            3'b001: if (x[rs1] != x[rs2])
                      pc <= pc + imm_b; // BNE
            3'b100: if ($signed(x[rs1]) < $signed(x[rs2]))
                      pc <= pc + imm_b; // BLT
            3'b101: if ($signed(x[rs1]) >= $signed(x[rs2]))
                      pc <= pc + imm_b; // BGE
            3'b110: if (x[rs1] < x[rs2])
                      pc <= pc + imm_b; // BLTU
            3'b111: if (x[rs1] >= x[rs2])
                      pc <= pc + imm_b; // BGEU
            default: halted <= 1;
          endcase
        end

        7'b0000011: begin // Loads
          if (rd != 0) begin
            case (funct3)
              3'b000: x[rd] <= {{24{load_word[7]}}, load_word[7:0]}; // LB
              3'b001: x[rd] <= {{16{load_word[15]}}, load_word[15:0]}; // LH
              3'b010: x[rd] <= load_word; // LW
              3'b100: x[rd] <= {24'b0, load_word[7:0]}; // LBU
              3'b101: x[rd] <= {16'b0, load_word[15:0]}; // LHU
              default: halted <= 1;
            endcase
          end
        end

        7'b0100011: begin // Stores are driven by the combinational data port.
          if (funct3 != 3'b000 && funct3 != 3'b001 && funct3 != 3'b010)
            halted <= 1;
        end

        7'b0010011: begin // Integer register-immediate operations
          if (rd != 0) begin
            case (funct3)
              3'b000: x[rd] <= x[rs1] + imm_i; // ADDI
              3'b010: x[rd] <= $signed(x[rs1]) < $signed(imm_i); // SLTI
              3'b011: x[rd] <= x[rs1] < imm_i; // SLTIU
              3'b100: x[rd] <= x[rs1] ^ imm_i; // XORI
              3'b110: x[rd] <= x[rs1] | imm_i; // ORI
              3'b111: x[rd] <= x[rs1] & imm_i; // ANDI
              3'b001: begin // SLLI
                if (funct7 == 7'b0000000)
                  x[rd] <= x[rs1] << instruction[24:20];
                else
                  halted <= 1;
              end
              3'b101: begin
                case (funct7)
                  7'b0000000: x[rd] <= x[rs1] >> instruction[24:20]; // SRLI
                  7'b0100000: x[rd] <= $signed(x[rs1])
                    >>> instruction[24:20]; // SRAI
                  default: halted <= 1;
                endcase
              end
              default: halted <= 1;
            endcase
          end
        end

        7'b0110011: begin // Integer register-register and M extension
          if (rd != 0) begin
            if (funct7 == 7'b0000001) begin
              case (funct3)
                3'b000: x[rd] <= x[rs1] * x[rs2]; // MUL
                3'b001: x[rd] <= product_ss[63:32]; // MULH
                3'b010: x[rd] <= product_su[63:32]; // MULHSU
                3'b011: x[rd] <= product_uu[63:32]; // MULHU
                3'b100: begin // DIV
                  if (x[rs2] == 0)
                    x[rd] <= 32'hffff_ffff;
                  else if (x[rs1] == 32'h8000_0000
                           && x[rs2] == 32'hffff_ffff)
                    x[rd] <= 32'h8000_0000;
                  else
                    x[rd] <= $signed(x[rs1]) / $signed(x[rs2]);
                end
                3'b101: x[rd] <= x[rs2] == 0
                  ? 32'hffff_ffff : x[rs1] / x[rs2]; // DIVU
                3'b110: begin // REM
                  if (x[rs2] == 0)
                    x[rd] <= x[rs1];
                  else if (x[rs1] == 32'h8000_0000
                           && x[rs2] == 32'hffff_ffff)
                    x[rd] <= 0;
                  else
                    x[rd] <= $signed(x[rs1]) % $signed(x[rs2]);
                end
                3'b111: x[rd] <= x[rs2] == 0
                  ? x[rs1] : x[rs1] % x[rs2]; // REMU
                default: halted <= 1;
              endcase
            end else begin
              case ({funct7, funct3})
                10'b0000000_000: x[rd] <= x[rs1] + x[rs2]; // ADD
                10'b0100000_000: x[rd] <= x[rs1] - x[rs2]; // SUB
                10'b0000000_001: x[rd] <= x[rs1] << x[rs2][4:0]; // SLL
                10'b0000000_010: x[rd] <= $signed(x[rs1])
                  < $signed(x[rs2]); // SLT
                10'b0000000_011: x[rd] <= x[rs1] < x[rs2]; // SLTU
                10'b0000000_100: x[rd] <= x[rs1] ^ x[rs2]; // XOR
                10'b0000000_101: x[rd] <= x[rs1] >> x[rs2][4:0]; // SRL
                10'b0100000_101: x[rd] <= $signed(x[rs1])
                  >>> x[rs2][4:0]; // SRA
                10'b0000000_110: x[rd] <= x[rs1] | x[rs2]; // OR
                10'b0000000_111: x[rd] <= x[rs1] & x[rs2]; // AND
                default: halted <= 1;
              endcase
            end
          end
        end

        7'b0001111: begin // FENCE is a no-op in this single-core model.
          if (funct3 != 3'b000 && funct3 != 3'b001)
            halted <= 1;
        end
        7'b1110011: begin // Stop on ECALL or EBREAK; no privileged extension.
          if (funct3 == 3'b000)
            halted <= 1;
          else
            halted <= 1;
        end
        default: halted <= 1;
      endcase
    end
  end
endmodule

module rv32im_demo;
  logic clk = 0;
  logic reset = 1;
  logic [31:0] instruction;
  logic [31:0] pc;
  logic [31:0] data_addr;
  logic [31:0] data_wdata;
  logic [31:0] data_rdata;
  logic [3:0] data_wstrb;
  logic halted;
  logic [31:0] result;
  logic [31:0] memory[0:63];
  integer j;

  rv32im_core cpu(.*);
  assign instruction = memory[pc[7:2]];
  assign data_rdata = memory[data_addr[7:2]];

  always @(posedge clk) begin
    if (data_wstrb[0]) memory[data_addr[7:2]][7:0]   <= data_wdata[7:0];
    if (data_wstrb[1]) memory[data_addr[7:2]][15:8]  <= data_wdata[15:8];
    if (data_wstrb[2]) memory[data_addr[7:2]][23:16] <= data_wdata[23:16];
    if (data_wstrb[3]) memory[data_addr[7:2]][31:24] <= data_wdata[31:24];
  end

  always #5 clk = ~clk;

  initial begin
    for (j = 0; j < 64; j = j + 1)
      memory[j] = 0;
    // 6 * 7 - 2 = 40, then verify a store, load, and taken branch.
    memory[0] = 32'h0060_0093; // addi x1,x0,6
    memory[1] = 32'h0070_0113; // addi x2,x0,7
    memory[2] = 32'h0220_81b3; // mul  x3,x1,x2
    memory[3] = 32'hffe1_8213; // addi x4,x3,-2
    memory[4] = 32'h0840_2023; // sw   x4,128(x0)
    memory[5] = 32'h0800_2283; // lw   x5,128(x0)
    memory[6] = 32'h0042_8463; // beq  x5,x4,+8
    memory[7] = 32'h0010_0073; // ebreak (failure path)
    memory[8] = 32'h0010_0313; // addi x6,x0,1 (pass flag)
    memory[9] = 32'h0010_0073; // ebreak

    $dumpfile("rv32im.vcd");
    $dumpvars(0, cpu);
    #12 reset = 0;
    #200 $fatal(1, "RV32IM core timed out");
  end

  always @(posedge clk) begin
    if (halted) begin
      assert (result == 1 && memory[32] == 40)
        else $fatal(1, "RV32IM self-test failed");
      $display("RV32IM self-test passed; memory[128] = %0d", memory[32]);
      $finish;
    end
  end
endmodule
`,
  },
  {
    name: 'UART transmitter',
    source: `// An 8-N-1 UART transmitter. Open Waveform after Run to inspect tx.
module uart_tx #(
  parameter integer CLOCKS_PER_BIT = 4
) (
  input  logic       clk,
  input  logic       reset,
  input  logic       start,
  input  logic [7:0] data,
  output logic       tx,
  output logic       busy,
  output logic       done
);
  logic [9:0] shifter;
  integer tick;
  integer bit_index;

  always @(posedge clk) begin
    if (reset) begin
      tx        <= 1;
      busy      <= 0;
      done      <= 0;
      tick      <= 0;
      bit_index <= 0;
      shifter   <= '1;
    end else begin
      done <= 0;
      if (start && !busy) begin
        shifter   <= {1'b1, data, 1'b0};
        tx        <= 0;
        busy      <= 1;
        tick      <= 0;
        bit_index <= 0;
      end else if (busy) begin
        if (tick == CLOCKS_PER_BIT - 1) begin
          tick      <= 0;
          shifter   <= {1'b1, shifter[9:1]};
          tx        <= shifter[1];
          bit_index <= bit_index + 1;
          if (bit_index == 9) begin
            tx   <= 1;
            busy <= 0;
            done <= 1;
          end
        end else begin
          tick <= tick + 1;
        end
      end
    end
  end
endmodule

module uart_demo;
  logic clk = 0;
  logic reset = 1;
  logic start = 0;
  logic [7:0] data = 0;
  logic tx;
  logic busy;
  logic done;
  integer sent = 0;

  uart_tx #(.CLOCKS_PER_BIT(4)) transmitter(.*);
  always #1 clk = ~clk;

  initial begin
    $dumpfile("uart.vcd");
    $dumpvars(0, uart_demo);
    #3 reset = 0;
    #2 data = "H"; start = 1;
    #2 start = 0;
  end

  always @(posedge clk) begin
    if (done) begin
      sent <= sent + 1;
      if (sent == 0) begin
        data  <= "i";
        start <= 1;
      end else begin
        $display("transmitted Hi as two 8-N-1 frames");
        $finish;
      end
    end else if (start && busy) begin
      start <= 0;
    end
  end

  initial #250 $fatal(1, "UART timed out");
endmodule
`,
  },
  {
    name: 'Tri-state buffer',
    source: `// Two tri-state buffers share a bus: idle is z and contention is x.
module tri_state_buffer (
  input  logic       enable,
  input  logic [3:0] data,
  inout  wire  [3:0] bus
);
  assign bus = enable ? data : 4'bzzzz;
endmodule

module tristate_demo;
  logic       drive_a = 0;
  logic       drive_b = 0;
  logic [3:0] value_a = 4'b1010;
  logic [3:0] value_b = 4'b1010;
  wire  [3:0] bus;

  tri_state_buffer buffer_a(.enable(drive_a), .data(value_a), .bus(bus));
  tri_state_buffer buffer_b(.enable(drive_b), .data(value_b), .bus(bus));

  initial begin
    $dumpfile("tristate-buffer.vcd");
    $dumpvars(0, tristate_demo);

    #1;
    assert (bus === 4'bzzzz);
    $display("idle:        bus=%b", bus);

    drive_a = 1;
    #1;
    assert (bus === 4'b1010);
    $display("device A:    bus=%b", bus);

    drive_b = 1;
    value_b = 4'b1001;
    #1;
    assert (bus === 4'b10xx);
    $display("contention:  bus=%b", bus);

    drive_a = 0;
    #1;
    assert (bus === 4'b1001);
    $display("device B:    bus=%b", bus);
    $finish;
  end
endmodule
`,
  },
  {
    name: 'Classes',
    source: `// A base-class handle dispatches to an overridden virtual method.
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
    source: `// Generate packet fields within declarative length and kind constraints.
module rnd;
  class Packet;
    rand bit [7:0] length;
    rand bit [3:0] kind;
    constraint c_length { length inside {[16:64]}; }
    constraint c_kind   { kind < 4; }
  endclass

  initial begin
    Packet p;
    p = new();
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
