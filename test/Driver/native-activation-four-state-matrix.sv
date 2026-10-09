// RUN: obelisk -O0 --vpi=off --native-scheduler=generic %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: %t.eval | FileCheck %s
// LRM 4.6/4.7: independent publications may be sliced, but the temporary's
// saved value precedes its overwrite. LRM 11.4.11 preserves the per-bit X/Z
// merge of ?:; 12.4 treats X/Z conditions as false. The case statement below
// instead retains state for unknown selectors (12.5 and 9.2.2.2.1).
// Exercise all 1024 combinations, with a sequential reference calculation
// that does not share the request/response scheduling graph.
// PLAN: obelisk statement scheduling: eligible=2 cohorts=1 plans=1 dataflow=1
// PLAN-NOT: checkpoint admission: {{.*}}__obelisk_region_kernel
// CHECK: checked 1024 four-state activations
module top;
  logic [1:0] seed, mask;
  logic mode;
  logic [1:0] request[1:-1], response[1:-1], result[1:-1];
  logic [1:0] temporary, held;

  function automatic logic digit(input int code);
    case (code)
      0: return 1'b0;
      1: return 1'b1;
      2: return 1'bx;
      default: return 1'bz;
    endcase
  endfunction

  always_comb begin
    temporary = seed ^ mask;
    for (int i = -1; i <= 1; ++i)
      result[i] = response[i] ^ temporary;
    request[-1] = mode ? seed : mask;
    request[0] = temporary;
    if (mode) request[0] = ~temporary;
    request[1] = seed;
    temporary = ~seed;
  end
  always_comb begin
    case (mode)
      1'b0: held = request[1];
      1'b1: held = ~request[1];
    endcase
    response[-1] = request[-1] & mask;
    response[0] = request[0] | mask;
    response[1] = held;
  end

  initial begin : oracle
    logic [1:0] reference_hold, saved, selected, conditional;
    int checked = 0;
    reference_hold = 'x;
    for (int a = 0; a < 16; ++a)
      for (int b = 0; b < 16; ++b)
        for (int c = 0; c < 4; ++c) begin
          seed = {digit(a / 4), digit(a % 4)};
          mask = {digit(b / 4), digit(b % 4)};
          mode = digit(c);
          saved = seed ^ mask;
          selected = mode ? seed : mask;
          conditional = c == 1 ? ~saved : saved;
          case (mode)
            1'b0: reference_hold = seed;
            1'b1: reference_hold = ~seed;
          endcase
          #1;
          if (result[-1] !== ((selected & mask) ^ saved) ||
              result[0] !== ((conditional | mask) ^ saved) ||
              result[1] !== (reference_hold ^ saved))
            $fatal(1, "four-state mismatch a=%0d b=%0d c=%0d result=%b/%b/%b",
                   a, b, c, result[-1], result[0], result[1]);
          ++checked;
        end
    $display("checked %0d four-state activations", checked);
    $finish;
  end
endmodule
