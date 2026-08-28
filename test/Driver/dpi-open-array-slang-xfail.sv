// XFAIL: *
// RUN: %obelisk -emit-obelisk %s -o /dev/null

// Slang v11 rejects these IEEE 1800-2017 7.7 / 35.6.1.1 legal bindings
// before Obelisk receives an AST. Keep the source regression visible without
// carrying a downstream Slang patch; compiler/runtime shape coverage lives in
// the Simulation IR and runtime tests.
module dpi_open_array_slang_xfail;
  import "DPI-C" task inspect_queue(input int values[]);
  import "DPI-C" task inspect_sized_dynamic(input int values[][0:1]);

  int queue_values[$];
  int fixed_dynamic[1:0][];

  initial begin
    inspect_queue(queue_values);
    inspect_sized_dynamic(fixed_dynamic);
  end
endmodule
