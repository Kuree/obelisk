"""Generate independent captures spanning multiple readiness words."""
print('module many_pending;\n  bit clk = 0;\n  int phase = 0;')
print('  always #5 clk = ~clk;\n  always @(posedge clk) phase <= phase + 1;')
for i in range(70):
    if i == 33:
        print("  logic [15:0] r33 = 16'h1200;")
        print('  always @(posedge clk) begin')
        print('    r33[15:8] = r33[15:8] + 1;')
        print('    r33[3:0] <= phase;\n  end')
    else:
        print(f'  int r{i} = {i};')
        print('  always @(posedge clk) begin')
        if i == 5:
            print("    if (phase == 1) r5 <= 'x;")
            print("    else if (phase == 2) r5 <= 'z;")
            print('    else if (phase == 3) r5 <= 8;')
            print('    else r5 <= r5 + 1;')
        elif i == 69:
            print('    r69[phase % 32] <= ~r69[phase % 32];')
        else:
            condition = 'if (phase != 3) ' if i == 64 else ''
            print(f'    {condition}r{i} <= r{i} + 1;')
            if i == 17:
                print('    r17 <= r17 + 2;')
        print('  end')
print('''  initial begin
    automatic string label = "many";
    repeat (6) begin
      @(posedge clk);
      $display("active %0d %0d %h", phase, r17, r33);
      #1;''')
print('      $display("%s %0d ' + ' '.join(['%h'] * 70) + '", label, phase, ' +
      ', '.join(f'r{i}' for i in range(70)) + ');')
print('''    end
    $finish;
  end
endmodule''')
