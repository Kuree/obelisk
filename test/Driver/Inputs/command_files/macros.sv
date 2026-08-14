module command_file_top;
  initial begin
`ifdef COMMAND_FILE_MACRO
    $display("command-file macro = %0d", `COMMAND_FILE_MACRO);
`endif
`ifdef NESTED_MACRO
    $display("nested macro = %0d", `NESTED_MACRO);
`endif
`ifdef QUOTED_MACRO
    $display("quoted macro = %0d", `QUOTED_MACRO);
`endif
`ifdef MACRO_FROM_ENV
    $display("env macro = %0d", `MACRO_FROM_ENV);
`endif
  end
endmodule
