// RUN: obelisk --std=1800-2023 -emit-slang %s -o /dev/null
// RUN: obelisk --std=1800-2023 -emit-obelisk %s -o /dev/null

interface generate_parameters_if;
  if (1) begin : chosen
    localparam int K = 42;
  end
endinterface

module generate_parameter_import;
  for (genvar idx = -1; idx < 3; idx += 3) begin : g
    localparam int K = 42;
    localparam bit [7:0] \L.p = 8'hfe;
    if (1) begin : inner
      localparam int K = 42;
    end
  end
  if (0) begin : inactive
    localparam int hidden = 123;
  end
  generate_parameters_if bus();
endmodule
