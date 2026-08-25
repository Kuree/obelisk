module leaf #(parameter int VALUE = 11);
endmodule

module pick #(parameter int VALUE = 21);
endmodule

module bound_probe #(parameter int VALUE = 31);
  leaf child();
endmodule
