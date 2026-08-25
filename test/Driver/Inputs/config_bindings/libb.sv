module leaf #(parameter int VALUE = 12);
endmodule

module pick #(parameter int VALUE = 22);
endmodule

module bound_probe #(parameter int VALUE = 32);
  leaf child();
endmodule
