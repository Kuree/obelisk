// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

`timescale 1ns / 1ns

primitive udp_level(output reg q = 1'b1, input d, enable);
  table
    ? 0 : ? : -;
    0 1 : ? : 0;
    1 1 : ? : 1;
    x 1 : ? : x;
  endtable
endprimitive

primitive udp_toggle(output reg q = 1'b0, input clock);
  table
    r : 0 : 1;
    r : 1 : 0;
    r : x : x;
    f : ? : -;
    (x0) : ? : -;
    (x1) : ? : -;
  endtable
endprimitive

primitive udp_positive(output reg q = 1'b0, input d, clock);
  table
    0 p : ? : 0;
    1 p : ? : 1;
    x p : ? : x;
    ? n : ? : -;
    * ? : ? : -;
  endtable
endprimitive

primitive udp_negative(output reg q = 1'b1, input clock);
  table
    n : ? : 0;
    p : ? : -;
  endtable
endprimitive

primitive udp_fall(output reg q = 1'b1, input clock);
  table
    f : ? : 0;
    p : ? : -;
  endtable
endprimitive

primitive udp_explicit_set(output reg q = 1'b0, input signal);
  table
    (b?) : ? : 1;
    (x0) : ? : -;
  endtable
endprimitive

primitive udp_any_change(output reg q = 1'b0, input signal);
  table
    * : ? : 1;
  endtable
endprimitive

primitive udp_no_init(output reg q, input signal);
  table
    r : ? : 1;
  endtable
endprimitive

primitive udp_missing_transition(output reg q = 1'b0, input clock);
  table
    r : ? : 1;
  endtable
endprimitive

primitive udp_initial_statement(q, d, enable);
  output q;
  reg q;
  input d, enable;
  initial q = 1'b1;
  table
    ? 0 : ? : -;
    0 1 : ? : 0;
    1 1 : ? : 1;
  endtable
endprimitive

primitive udp_mixed(output reg q = 1'b0, input d, clock, reset);
  table
    0 r ? : ? : 0;
    1 r ? : ? : 1;
    x r ? : ? : x;
    ? ? 1 : ? : 0;
    ? (x0) ? : ? : -;
    ? (x1) ? : ? : -;
  endtable
endprimitive

module sequential_udp;
  logic d, enable, clock, negative_clock, fall_clock, explicit_signal,
        any_signal, missing_clock, reset;
  wire level_q, toggle_q, positive_q, negative_q, fall_q, explicit_q, any_q,
       no_init_q, missing_q, initial_statement_q, mixed_q;
  udp_level level_instance(level_q, d, enable);
  udp_toggle toggle_instance(toggle_q, clock);
  udp_positive positive_instance(positive_q, d, clock);
  udp_negative negative_instance(negative_q, negative_clock);
  udp_fall fall_instance(fall_q, fall_clock);
  udp_explicit_set explicit_instance(explicit_q, explicit_signal);
  udp_any_change any_instance(any_q, any_signal);
  udp_no_init no_init_instance(no_init_q, any_signal);
  udp_missing_transition missing_instance(missing_q, missing_clock);
  udp_initial_statement initial_statement_instance(initial_statement_q, d,
                                                    enable);
  udp_mixed mixed_instance(mixed_q, d, clock, reset);

  logic [3:0] array_d, array_enable;
  wire [3:0] array_q;
  udp_level level_array[3:0](array_q, array_d, array_enable);

  logic delayed_d, delayed_clock;
  wire delayed_q;
  udp_positive #(3, 5) delayed_instance(delayed_q, delayed_d,
                                        delayed_clock);

  logic cancel_d, cancel_clock;
  wire cancel_q;
  udp_positive #5 cancellation_instance(cancel_q, cancel_d, cancel_clock);

  logic strength_clock, strength_compete;
  wire strength_q;
  udp_toggle (strong0, weak1) strength_instance(strength_q, strength_clock);
  bufif1 (pull0, pull1) strength_competitor(strength_q, 1'b0,
                                            strength_compete);

  initial begin
    // Initial values are published at time zero. The X->1 transition of
    // clock is also a positive edge, so udp_positive samples d immediately.
    d = 1;
    enable = 0;
    clock = 1;
    negative_clock = 1;
    fall_clock = 1;
    explicit_signal = 0;
    any_signal = 'x;
    reset = 0;
    array_d = 4'b0101;
    array_enable = 4'b0000;
    delayed_d = 0;
    delayed_clock = 0;
    cancel_d = 0;
    cancel_clock = 0;
    strength_clock = 0;
    strength_compete = 1;
    #1;
    if (level_q !== 1 || positive_q !== 1 || toggle_q !== 0 ||
        negative_q !== 1 || fall_q !== 1 || explicit_q !== 0 || any_q !== 0 ||
        no_init_q !== 'x || missing_q !== 0 || initial_statement_q !== 1 ||
        mixed_q !== 0 || array_q !== 4'b1111 || strength_q !== 0)
      $fatal(0, "sequential UDP initialization mismatch");

    // No input transition holds the declaration initializer, but an actual
    // input transition omitted from the table produces X.
    missing_clock = 0;
    #1 if (missing_q !== 'x) $fatal(0, "missing transition must produce X");

    // Level rows and '-' retain state; Z inputs normalize to X.
    enable = 1;
    #1 if (level_q !== 1) $fatal(0, "level set mismatch");
    d = 0;
    #1 if (level_q !== 0) $fatal(0, "level clear mismatch");
    enable = 0;
    d = 1;
    #1 if (level_q !== 0) $fatal(0, "level hold mismatch");
    enable = 1;
    d = 'z;
    #1 if (level_q !== 'x) $fatal(0, "level Z-to-X mismatch");

    // State-column matching toggles only on exact rises; the explicit falling
    // row holds state.
    clock = 0;
    #1;
    clock = 1;
    #1 if (toggle_q !== 1) $fatal(0, "toggle state 0 mismatch");
    clock = 0;
    #1 if (toggle_q !== 1) $fatal(0, "toggle falling hold mismatch");
    clock = 1;
    #1 if (toggle_q !== 0) $fatal(0, "toggle state 1 mismatch");

    // Edge matching canonicalizes Z to X.  A 0->Z transition is positive,
    // Z->1 is positive, and X->Z is no transition after normalization.
    clock = 0;
    d = 0;
    #1 clock = 'z;
    #1 if (positive_q !== 0) $fatal(0, "positive 0-to-Z mismatch");
    d = 1;
    clock = 1;
    #1 if (positive_q !== 1) $fatal(0, "positive Z-to-1 mismatch");
    any_signal = 'z;
    #1 if (any_q !== 0) $fatal(0, "X-to-Z must not be an edge");
    any_signal = 0;
    #1 if (any_q !== 1) $fatal(0, "Z-to-0 any-edge mismatch");

    negative_clock = 'z;
    #1 if (negative_q !== 0) $fatal(0, "negative edge mismatch");
    fall_clock = 0;
    #1 if (fall_q !== 0) $fatal(0, "exact fall mismatch");
    explicit_signal = 'x;
    #1 if (explicit_q !== 1) $fatal(0, "explicit wildcard edge mismatch");

    // The reset level and a clock rise arrive in one activation. The reset
    // edge row appears first and is also applicable, but the later reset level
    // row has the unconditional priority required by 29.9.
    d = 1;
    clock = 0;
    #1;
    clock = 1;
    reset = 1;
    #1 if (mixed_q !== 0) $fatal(0, "mixed reset/edge dominance mismatch");
    reset = 0;
    clock = 0;
    #1;
    clock = 1;
    #1 if (mixed_q !== 1) $fatal(0, "mixed edge mismatch");

    array_enable = 4'b1111;
    #1 if (array_q !== 4'b0101) $fatal(0, "sequential UDP array mismatch");

    // The weak 1 state is hidden by the enabled pull-strength zero driver.
    // Disabling that competitor reveals the committed raw UDP state.
    strength_clock = 1;
    #1 if (strength_q !== 0) $fatal(0, "strength competitor mismatch");
    strength_compete = 0;
    #1 if (strength_q !== 1) $fatal(0, "strength raw state mismatch");

    // Two-value delays use the committed raw driver state.
    delayed_d = 1;
    delayed_clock = 1;
    #2 if (delayed_q !== 0) $fatal(0, "sequential rise fired early");
    #1 if (delayed_q !== 1) $fatal(0, "sequential rise delay mismatch");
    delayed_clock = 0;
    delayed_d = 0;
    #1 delayed_clock = 1;
    #4 if (delayed_q !== 1) $fatal(0, "sequential fall fired early");
    #1 if (delayed_q !== 0) $fatal(0, "sequential fall delay mismatch");

    // A second transition before the delayed state update still sees state 0
    // and cancels the pending rise by scheduling the committed value again.
    cancel_d = 1;
    cancel_clock = 1;
    #1 cancel_clock = 0;
    cancel_d = 0;
    #1 cancel_clock = 1;
    #6 if (cancel_q !== 0) $fatal(0, "sequential delayed cancellation mismatch");

    $display("SEQUENTIAL UDP PASS");
  end
endmodule

// CHECK: SEQUENTIAL UDP PASS
