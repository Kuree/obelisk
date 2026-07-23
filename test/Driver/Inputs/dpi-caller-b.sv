module dpi_caller_b;
  import "DPI-C" context dpi_caller = function int caller();
  initial begin
    $display("caller-b %0d", caller());
  end
endmodule
