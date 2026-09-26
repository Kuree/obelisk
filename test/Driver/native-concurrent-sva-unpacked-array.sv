// RUN: obelisk --compile-threads=1 %s -o %t.native

module native_concurrent_sva_unpacked_array(
    input logic clock, reset_n, acknowledge);
  typedef struct packed {
    logic valid;
    logic is_read;
  } pending_t;
  pending_t pending[2];

  always_ff @(posedge clock) begin
    pending[0] <= '0;
    pending[1] <= '0;
  end

  assert property (@(posedge clock) disable iff (!reset_n)
      acknowledge |-> pending[0].valid);
endmodule
