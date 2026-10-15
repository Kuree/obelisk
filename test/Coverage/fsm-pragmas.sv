// IEEE 1800-2023 40.2.2 and 40.4: FSM state coverage counts the states of
// each FSM that pragmas identify which its state vector held. The forms are
// a signal with a separate enumeration pragma in its declaration (40.4.1) in
// one-line comments (40.4.7), a part-select (40.4.2), and a concatenation
// (40.4.3). States take each instance's parameter values. The initial state
// counts; DONE and M1 are never reached.
module ctrl #(parameter [1:0] BUSY_CODE = 2) (input bit clk);
  /* tool state_vector cs */
  logic [1:0] // tool enum CTRL
    cs, ns;
  parameter [1:0] // tool enum CTRL
    IDLE = 0,
    BUSY = BUSY_CODE,
    DONE = 3;
  /* tool state_vector word[5:4] MODE enum M */
  bit [7:0] word;
  parameter /* tool enum M */ M0 = 0, M1 = 1, M2 = 2;
  /* tool state_vector {hi, lo} PAIR enum P */
  bit hi, lo;
  localparam /* tool enum P */ P0 = 2'b00, P3 = 2'b11;
  initial cs = IDLE;
  always @(posedge clk) begin
    cs <= ns;
    word[5:4] <= 2'd2;
    {hi, lo} <= 2'b11;
  end
  always_comb ns = (cs == IDLE) ? BUSY : cs;
endmodule

module top;
  bit clk;
  ctrl #(.BUSY_CODE(1)) u1(clk);
  ctrl u2(clk);
  initial begin
    #1 clk = 1;
    #1 $display("%0d/%0d %0d/%0d",
                $coverage_get(`SV_COV_FSM_STATE, `SV_COV_MODULE, top.u1),
                $coverage_get_max(`SV_COV_FSM_STATE, `SV_COV_MODULE, top.u1),
                $coverage_get(`SV_COV_FSM_STATE, `SV_COV_HIER, top),
                $coverage_get_max(`SV_COV_FSM_STATE, `SV_COV_HIER, top));
  end
endmodule

// RUN: %obelisk --std=1800-2023 -O0 --coverage=fsm %s -o %t
// RUN: %t --coverage-output=%t.obcov | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk-cov report --format=text %t.obcov | FileCheck %s --check-prefix=REPORT
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode --coverage=fsm %s -o %t.bytecode
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov | FileCheck %s --check-prefix=RUNTIME

// RUNTIME: 6/8 12/16
// REPORT: fsm: 75.00% (12/16)
