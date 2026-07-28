// RUN: obelisk -O0 %s -o %t

module unsupported_unpacked_selection;
  typedef struct {
    logic [31:0] elements [3:0];
  } aggregate_t;

  aggregate_t value;

  initial value.elements[3:1] = '{32'd4, 32'd3, 32'd2};
endmodule

// Unpacked aggregate slice assignments are lowered element by element.
