module default_delay(output bit done = 0);
  initial #1 done = 1;
endmodule
