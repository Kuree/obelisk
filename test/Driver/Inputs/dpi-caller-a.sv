module dpi_caller_a;
  import "DPI-C" context dpi_caller = function int caller();
  initial begin
    $display("caller-a %0d", caller());
  end
endmodule
