export const EXAMPLES = [
  {
    name: 'Streaming FIFO',
    source: `// Ready/valid FIFO with an asynchronous read port.
module stream_fifo #(
  parameter int WIDTH = 16,
  parameter int DEPTH = 4,
  localparam int PTR_W = DEPTH > 1 ? $clog2(DEPTH) : 1
) (
  input  logic clk, reset,
  input  logic [WIDTH-1:0] in_data,
  input  logic in_valid, out_ready,
  output logic in_ready, out_valid,
  output logic [WIDTH-1:0] out_data,
  output logic [$clog2(DEPTH+1)-1:0] level
);
  timeunit 1ns;
  timeprecision 1ps;

  logic [WIDTH-1:0] memory [DEPTH];
  logic [PTR_W-1:0] read_ptr, write_ptr;
  wire push = in_valid && in_ready;
  wire pop = out_valid && out_ready;

  assign in_ready = level < DEPTH;
  assign out_valid = level != 0;
  assign out_data = memory[read_ptr];

  always_ff @(posedge clk) begin
    if (reset) begin
      read_ptr <= 0;
      write_ptr <= 0;
      level <= 0;
    end else begin
      if (push) begin
        memory[write_ptr] <= in_data;
        write_ptr <= write_ptr == DEPTH-1 ? 0 : write_ptr + 1;
      end
      if (pop)
        read_ptr <= read_ptr == DEPTH-1 ? 0 : read_ptr + 1;
      case ({push, pop})
        2'b10: level <= level + 1;
        2'b01: level <= level - 1;
        default: ;
      endcase
    end
  end

  hold_output: assert property (
    @(posedge clk) disable iff (reset)
      out_valid && !out_ready |=> out_valid && $stable(out_data)
  ) else $fatal(1, "output changed under backpressure");
endmodule

module fifo_demo;
  timeunit 1ns;
  timeprecision 1ps;

  logic clk = 0, reset = 1;
  logic [15:0] in_data = 0, out_data;
  logic in_valid = 0, out_ready = 0;
  logic in_ready, out_valid;
  logic [2:0] level;
  logic [15:0] expected[$];
  logic [15:0] next_word;
  int sent = 0, received = 0, stalls = 0, full_cycles = 0;
  bit accepted = 0;

  stream_fifo fifo(.*);
  always #5 clk = ~clk;

  initial begin
    $dumpfile("streaming-fifo.vcd");
    $dumpvars(0, fifo_demo);
    repeat (2) @(negedge clk);
    reset = 0;

    for (int cycle = 0; cycle < 160; cycle++) begin
      // Fill the FIFO first, then stall each side independently.
      if (!in_valid || accepted)
        in_valid = sent < 32 && cycle % 7 != 3;
      in_data = 16'ha500 ^ sent;
      out_ready = cycle >= 12 && cycle % 5 >= 2;

      @(posedge clk);
      accepted = in_valid && in_ready;
      if (accepted) begin
        expected.push_back(in_data);
        sent++;
      end
      if (out_valid && out_ready) begin
        assert (expected.size() != 0) else $fatal(1, "unexpected output");
        next_word = expected.pop_front();
        assert (out_data === next_word)
          else $fatal(1, "word %0d: got %h, expected %h",
                      received, out_data, next_word);
        received++;
      end
      if (in_valid && !in_ready) stalls++;
      if (level == 4) full_cycles++;

      if (received == 32) begin
        assert (sent == 32 && expected.size() == 0 &&
                stalls > 0 && full_cycles > 0)
          else $fatal(1, "traffic did not exercise a full FIFO");
        $display("PASS: Streaming FIFO; %0d words, %0d stalled cycles",
                 received, stalls);
        $finish;
      end
      @(negedge clk);
    end
    $fatal(1, "FIFO timed out");
  end
endmodule
`,
  },
  {
    name: 'CRC-32 packets',
    source: `// Reflected CRC-32: polynomial 0xedb88320, initial/final XOR 0xffffffff.
module crc32_stream (
  input  logic clk, reset,
  input  logic valid, last,
  input  logic [7:0] data,
  output logic done,
  output logic [31:0] checksum
);
  timeunit 1ns;
  timeprecision 1ps;

  logic [31:0] state, next_state;

  always_comb begin
    next_state = state ^ {24'b0, data};
    for (int bit_index = 0; bit_index < 8; bit_index++)
      next_state = (next_state >> 1) ^
                   (next_state[0] ? 32'hedb8_8320 : 32'b0);
  end

  always_ff @(posedge clk) begin
    if (reset) begin
      state <= '1;
      done <= 0;
      checksum <= 0;
    end else begin
      done <= 0;
      if (valid) begin
        state <= last ? '1 : next_state;
        if (last) begin
          checksum <= ~next_state;
          done <= 1;
        end
      end
    end
  end
endmodule

module crc_demo;
  timeunit 1ns;
  timeprecision 1ps;

  logic clk = 0, reset = 1;
  logic valid = 0, last = 0, done;
  logic [7:0] data = 0;
  logic [31:0] checksum;
  bit [31:0] reference_crc;
  string packets[4] = '{"123456789", "talos", "RTL simulation", "A"};

  crc32_stream dut(.*);
  always #5 clk = ~clk;

  initial begin
    $dumpfile("crc32-packets.vcd");
    $dumpvars(0, crc_demo);
    repeat (2) @(negedge clk);
    reset = 0;

    foreach (packets[p]) begin
      reference_crc = '1;
      for (int i = 0; i < packets[p].len(); i++) begin
        // Leave a bubble between bytes.
        valid = 0;
        @(negedge clk);
        data = packets[p].getc(i);
        valid = 1;
        last = i == packets[p].len()-1;
        for (int b = 0; b < 8; b++) begin
          bit feedback;
          feedback = reference_crc[0] ^ data[b];
          reference_crc >>= 1;
          if (feedback) reference_crc ^= 32'hedb8_8320;
        end
        @(negedge clk);
      end
      valid = 0;
      last = 0;
      assert (done && checksum === ~reference_crc)
        else $fatal(1, "CRC mismatch for packet %0d", p);
      if (p == 0)
        assert (checksum == 32'hcbf4_3926)
          else $fatal(1, "CRC-32 check vector failed");
      $display("packet %0d: \\"%s\\" -> CRC-32 %08h", p, packets[p], checksum);
      @(negedge clk);
    end
    $display("PASS: CRC-32 packets; 4 packets with input bubbles");
    $finish;
  end
endmodule
`,
  },
  {
    name: 'SystemVerilog assertions',
    source: `// Grant follows request; two busy cycles precede completion.
module sva_protocol;
  logic clk = 0;
  logic rst = 1;
  logic req = 0;
  logic grant = 0;
  logic busy = 0;
  logic done = 0;
  logic error = 0;
  int completed = 0;

  sequence response_path;
    grant ##1 busy[*2] ##1 (done && !error);
  endsequence

  property bounded_response;
    @(posedge clk) disable iff (rst)
      req |=> response_path;
  endproperty

  response_check: assert property (bounded_response)
    else $error("request did not receive a bounded response");
  // Count completed responses, excluding vacuous passes while idle.
  response_coverage: cover property (
    @(posedge clk) disable iff (rst) req ##1 response_path
  ) completed++;

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

    assert (completed == 2) else $fatal(1, "expected two completed responses");
    $display("PASS: SystemVerilog assertions; 2 responses, 1 reset cancellation");
    $finish;
  end
endmodule
`,
  },
  {
    name: 'RV32IM core',
    source: `// Single-cycle RV32IM core with separate instruction and data ports.
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
      $display("PASS: RV32IM core; memory[128] = %0d", memory[32]);
      $finish;
    end
  end
endmodule
`,
  },
  {
    name: 'UART transmitter',
    source: `// 8-N-1 transmitter with a bit-sampling loopback test.
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
  timeunit 1ns;
  timeprecision 1ps;

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
  timeunit 1ns;
  timeprecision 1ps;

  localparam int CLOCKS_PER_BIT = 4;
  logic clk = 0, reset = 1, start = 0;
  logic [7:0] data = 0;
  logic tx, busy, done;
  string message = "Hello, talos!";
  int checked = 0;

  uart_tx #(.CLOCKS_PER_BIT(CLOCKS_PER_BIT)) transmitter(.*);
  always #1 clk = ~clk;

  initial begin
    $dumpfile("uart-loopback.vcd");
    $dumpvars(0, uart_demo);
    repeat (2) @(negedge clk);
    reset = 0;

    for (int i = 0; i < message.len(); i++) begin
      @(negedge clk);
      data = message.getc(i);
      start = 1;
      @(negedge clk);
      start = 0;
      wait (done);
      @(negedge clk);
    end
    wait (checked == message.len());
    $display("PASS: UART transmitter; decoded %0d 8-N-1 frames: %s",
             checked, message);
    $finish;
  end

  // Sample at bit centers, including the start and stop bits.
  initial begin
    bit [7:0] received;
    for (int frame = 0; frame < message.len(); frame++) begin
      @(negedge tx);
      repeat (CLOCKS_PER_BIT/2) @(posedge clk);
      #1;
      assert (tx === 1'b0) else $fatal(1, "invalid UART start bit");
      for (int b = 0; b < 8; b++) begin
        repeat (CLOCKS_PER_BIT) @(posedge clk);
        #1;
        assert (!$isunknown(tx)) else $fatal(1, "unknown UART data bit");
        received[b] = tx;
      end
      repeat (CLOCKS_PER_BIT) @(posedge clk);
      #1;
      assert (tx === 1'b1) else $fatal(1, "invalid UART stop bit");
      assert (received == message.getc(frame))
        else $fatal(1, "UART frame %0d: got %02h", frame, received);
      checked++;
      $display("rx[%0d] = %c (%02h)", frame, received, received);
    end
  end

  initial #2000 $fatal(1, "UART timed out");
endmodule
`,
  },
  {
    name: 'Open-drain arbitration',
    source: `// Open-drain arbitration: a master that sends 1 and reads 0 drops out.
module bus_master #(parameter bit [7:0] WORD = 8'ha6) (
  input  logic scl, start,
  output logic lost = 0, done = 0,
  inout  wire sda
);
  timeunit 1ns;
  timeprecision 1ps;

  logic pull_low = 0;
  nmos driver(sda, 1'b0, pull_low);

  initial begin
    wait (start);
    for (int b = 7; b >= 0; b--) begin
      @(negedge scl);
      pull_low = !lost && !WORD[b];
      @(posedge scl);
      if (!lost && WORD[b] && sda === 1'b0)
        lost = 1;
    end
    @(negedge scl);
    pull_low = 0;
    done = 1;
  end
endmodule

module open_drain_demo;
  timeunit 1ns;
  timeprecision 1ps;

  logic scl = 1, start = 0, connected = 1;
  logic lost_a, lost_b, done_a, done_b;
  logic fault_high = 0, fault_low = 0;
  bit [7:0] observed = 0;
  wire sda, remote_sda;

  pullup bias(sda);
  tranif1 bridge(sda, remote_sda, connected);
  bus_master #(.WORD(8'ha6)) master_a(
    scl, start, lost_a, done_a, sda
  );
  bus_master #(.WORD(8'ha8)) master_b(
    scl, start, lost_b, done_b, remote_sda
  );

  // Inject a push-pull fault after arbitration.
  bufif1 (strong1, strong0) bad_high(sda, 1'b1, fault_high);
  bufif1 (strong1, strong0) bad_low(sda, 1'b0, fault_low);
  always #5 scl = ~scl;

  initial begin
    $dumpfile("open-drain-arbitration.vcd");
    $dumpvars(0, open_drain_demo);
    #1;
    assert (sda === 1'b1 && remote_sda === 1'b1)
      else $fatal(1, "pull-up did not cross the bus switch");
    $display("idle: local=%v remote=%v", sda, remote_sda);
    start = 1;

    for (int b = 7; b >= 0; b--) begin
      @(posedge scl);
      observed[b] = sda;
      $display("bit %0d: bus=%v", b, sda);
    end
    wait (done_a && done_b);
    #1;
    assert (observed == 8'ha6 && !lost_a && lost_b && sda === 1'b1)
      else $fatal(1, "arbitration did not select master A");
    $display("master A won: %02h; master B released the line", observed);

    connected = 0;
    #8;
    assert (remote_sda === 1'bz && sda === 1'b1)
      else $fatal(1, "isolated bus segment did not float");
    $display("isolated: local=%v remote=%v", sda, remote_sda);
    connected = 1;
    fault_high = 1;
    fault_low = 1;
    #8;
    assert (sda === 1'bx && remote_sda === 1'bx)
      else $fatal(1, "equal-strength contention must resolve to x");
    $display("contention: local=%v remote=%v", sda, remote_sda);
    fault_high = 0;
    #8;
    assert (sda === 1'b0)
      else $fatal(1, "strong zero must beat the pull-up");
    fault_low = 0;
    #8;
    assert (sda === 1'b1 && remote_sda === 1'b1)
      else $fatal(1, "bus did not recover after contention");
    $display("PASS: Open-drain arbitration; pull-up, isolation, contention, recovery");
    $finish;
  end

  initial #200 $fatal(1, "bus arbitration timed out");
endmodule
`,
  },
  {
    name: 'Switch-level charge',
    source: `// CMOS sample/hold with a 12 ns trireg decay time.
module charge_demo;
  timeunit 1ns;
  timeprecision 1ps;

  logic sample = 0, data = 1;
  trireg (medium) #(0, 0, 12) storage;
  cmos sampling_switch(storage, data, sample, !sample);

  // Each rtran reduces the drive strength: supply -> pull -> weak.
  wire source, resistive_1, resistive_2;
  logic sink = 0;
  assign (supply1, supply0) source = 1'b1;
  rtran r1(source, resistive_1);
  rtran r2(resistive_1, resistive_2);
  bufif1 (strong0, strong1) load(resistive_2, 1'b0, sink);

  initial begin
    $dumpfile("switch-level-charge.vcd");
    $dumpvars(0, charge_demo);
    sample = 1;
    #2;
    assert (storage === 1'b1) else $fatal(1, "sample did not charge the node");
    $display("sampling: stored=%b", storage);

    sample = 0;
    data = 0;
    #5;
    assert (storage === 1'b1) else $fatal(1, "disconnected charge was lost");
    $display("holding after input changed: stored=%b", storage);

    // Refresh before the first decay timer expires.
    sample = 1;
    #2;
    assert (storage === 1'b0) else $fatal(1, "refresh did not write zero");
    sample = 0;
    #6;
    assert (storage === 1'b0) else $fatal(1, "stale decay event was not cancelled");
    $display("refreshed past the original expiry: stored=%b", storage);
    #7;
    assert (storage === 1'bx) else $fatal(1, "stored charge did not decay");
    $display("decayed: stored=%b", storage);

    assert ({source, resistive_1, resistive_2} === 3'b111)
      else $fatal(1, "resistive chain did not propagate one");
    $display("resistive path: %v -> %v -> %v",
             source, resistive_1, resistive_2);
    sink = 1;
    #1;
    assert (source === 1'b1 && resistive_2 === 1'b0)
      else $fatal(1, "strong local load did not beat the attenuated drive");
    $display("loaded path: %v -> %v -> %v",
             source, resistive_1, resistive_2);
    #5;
    sink = 0;
    sample = 1;
    #2;
    assert (storage === 1'b0 && resistive_2 === 1'b1)
      else $fatal(1, "recharge or resistive-path recovery failed");
    sample = 0;
    $display("PASS: Switch-level charge; sample, hold, refresh, decay, resistive drive");
    $finish;
  end
endmodule
`,
  },
  {
    name: 'Class-based scoreboard',
    source: `// Three transaction types check a two-stage datapath.
module scoreboard_demo;
  timeunit 1ns;
  timeprecision 1ps;

  class Transaction;
    int id;
    bit [31:0] data, key;

    function new(int id, bit [31:0] data, bit [31:0] key = 0);
      this.id = id;
      this.data = data;
      this.key = key;
    endfunction

    virtual function bit [1:0] opcode();
      return 0;
    endfunction

    virtual function bit [31:0] predict();
      return data;
    endfunction
  endclass

  class XorTransaction extends Transaction;
    function new(int id, bit [31:0] data, bit [31:0] key);
      super.new(id, data, key);
    endfunction
    virtual function bit [1:0] opcode();
      return 1;
    endfunction
    virtual function bit [31:0] predict();
      return data ^ key;
    endfunction
  endclass

  class RotateTransaction extends Transaction;
    function new(int id, bit [31:0] data);
      super.new(id, data);
    endfunction
    virtual function bit [1:0] opcode();
      return 2;
    endfunction
    virtual function bit [31:0] predict();
      return (data << 8) | (data >> 24);
    endfunction
  endclass

  class Scoreboard;
    Transaction pending[$];
    int checked = 0;

    function void submit(Transaction item);
      pending.push_back(item);
    endfunction

    function void check(logic [31:0] actual);
      Transaction item;
      assert (pending.size() != 0) else $fatal(1, "unexpected pipeline result");
      item = pending.pop_front();
      assert (actual === item.predict())
        else $fatal(1, "transaction %0d: got %08h, expected %08h",
                    item.id, actual, item.predict());
      checked++;
    endfunction
  endclass

  logic clk = 0, reset = 1, valid = 0;
  logic [31:0] data = 0, key = 0, stage_data, stage_key, result;
  logic [1:0] opcode = 0, stage_op;
  logic stage_valid, result_valid;
  Scoreboard scoreboard;
  Transaction item;

  always #5 clk = ~clk;

  // Rotate with wiring; the reference model uses shifts.
  always_ff @(posedge clk) begin
    if (reset) begin
      stage_valid <= 0;
      result_valid <= 0;
    end else begin
      stage_valid <= valid;
      stage_data <= data;
      stage_key <= key;
      stage_op <= opcode;
      result_valid <= stage_valid;
      if (stage_valid) begin
        case (stage_op)
          0: result <= stage_data;
          1: result <= stage_data ^ stage_key;
          2: result <= {stage_data[23:0], stage_data[31:24]};
          default: result <= 'x;
        endcase
      end
    end
  end

  always @(posedge clk)
    if (!reset && result_valid) scoreboard.check(result);

  initial begin
    scoreboard = new;
    $dumpfile("class-scoreboard.vcd");
    $dumpvars(0, scoreboard_demo);
    repeat (2) @(negedge clk);
    reset = 0;
    for (int i = 0; i < 18; i++) begin
      case (i % 3)
        0: item = new(i, 32'h1234_5678 + i);
        1: item = XorTransaction::new(i, 32'h1234_5678 + i, 32'ha5a5_5a5a);
        2: item = RotateTransaction::new(i, 32'h1234_5678 + i);
      endcase
      scoreboard.submit(item);
      data = item.data;
      key = item.key;
      opcode = item.opcode();
      valid = 1;
      @(negedge clk);
      valid = 0;
      // Occasional input bubbles.
      if (i % 4 == 0) @(negedge clk);
    end
    wait (scoreboard.checked == 18);
    assert (scoreboard.pending.size() == 0)
      else $fatal(1, "scoreboard did not drain");
    $display("PASS: Class-based scoreboard; 18 transactions, 3 virtual models");
    $finish;
  end

  initial #500 $fatal(1, "scoreboard timed out");
endmodule
`,
  },
  {
    name: 'Constrained-random packets',
    source: `// Aligned reads and writes, with four tags per randc round.
module random_demo;
  timeunit 1ns;
  timeprecision 1ps;

  class Packet;
    randc bit [1:0] tag;
    rand bit write;
    rand bit [5:0] length;
    rand bit [7:0] address;
    rand bit [7:0] payload[4];
    bit [7:0] checksum;

    constraint legal_packet {
      length inside {[4:16]};
      length[1:0] == 0;
      address[1:0] == 0;
      address + int'(length) <= 256;
      !write -> length == 4;
      foreach (payload[i]) payload[i] inside {[0:15]};
    }

    function void post_randomize();
      checksum = 0;
      foreach (payload[i]) checksum ^= payload[i];
    endfunction
  endclass

  covergroup traffic with function sample(bit [1:0] tag, bit write);
    tags: coverpoint tag { bins each_tag[] = {[0:3]}; }
    direction: coverpoint write { bins read = {0}; bins write_op = {1}; }
    combinations: cross tags, direction;
  endgroup

  Packet packet;
  traffic coverage;
  bit [3:0] seen;
  bit [7:0] checksum;
  int covered, total;
  real percentage;

  initial begin
    packet = new;
    packet.srandom(42);
    coverage = new;

    // One read round and one write round cover all eight tag/direction pairs.
    for (int round = 0; round < 2; round++) begin
      packet.write = round != 0;
      packet.write.rand_mode(0);
      seen = 0;
      repeat (4) begin
        assert (packet.randomize()) else $fatal(1, "packet constraints failed");
        assert (!seen[packet.tag]) else $fatal(1, "randc tag repeated");
        seen[packet.tag] = 1;
        assert (packet.length inside {[4:16]} && packet.length % 4 == 0 &&
                packet.address % 4 == 0 &&
                int'(packet.address) + int'(packet.length) <= 256 &&
                (packet.write || packet.length == 4))
          else $fatal(1, "generated packet violated the protocol");
        checksum = 0;
        foreach (packet.payload[i]) begin
          assert (packet.payload[i] < 16) else $fatal(1, "payload constraint failed");
          checksum ^= packet.payload[i];
        end
        assert (packet.checksum == checksum) else $fatal(1, "post_randomize failed");
        coverage.sample(packet.tag, packet.write);
        $display("%s tag=%0d address=%02h length=%0d checksum=%02h",
                 packet.write ? "write" : "read ", packet.tag,
                 packet.address, packet.length, packet.checksum);
        #1;
      end
      assert (seen == 4'b1111) else $fatal(1, "randc round missed a tag");
    end

    percentage = coverage.get_inst_coverage(covered, total);
    assert (percentage == 100.0 && covered == total && total == 14)
      else $fatal(1, "coverage incomplete: %0d/%0d", covered, total);
    $display("PASS: Constrained-random packets; %0d/%0d coverage bins (%.0f%%)",
             covered, total, percentage);
    $finish;
  end
endmodule
`,
  },
];
