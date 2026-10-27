// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// CHECK-DAG: %[[FORMAT:.*]] = simulation.bytes.constant "value=%0h"
// CHECK-DAG: %[[STDOUT:.*]] = arith.constant 1 : i32
// CHECK-DAG: %[[OUT_PATH:.*]] = simulation.bytes.constant "out.log"
// CHECK-DAG: %[[INPUT_PATH:.*]] = simulation.bytes.constant "input.bin"
// CHECK-DAG: %[[READ_MODE:.*]] = simulation.bytes.constant "rb"
// CHECK-DAG: simulation.bytes.constant "value="
// CHECK-DAG: %[[ALL_FILES:.*]] = arith.constant 0 : i32
// CHECK: simulation.display {{.*}} to %[[STDOUT]](%[[FORMAT]], {{.*}}) newline = true radix = <decimal> flags = [0, 0] {library_cell = "work.system_io", scope = "system_io", time_multiplier = 1000 : i64, time_precision = -12 : i32}
// CHECK: simulation.display {{.*}} newline = true radix = <decimal> flags = [0, 2, 0] {library_cell = "work.system_io", scope = "system_io", time_multiplier = 1000 : i64, time_precision = -12 : i32}
// CHECK: simulation.display {{.*}} newline = true radix = <decimal> flags = [0, 1] {library_cell = "work.system_io", scope = "system_io.named", time_multiplier = 1000 : i64, time_precision = -12 : i32}
// CHECK: simulation.display {{.*}} newline = true radix = <binary>
// CHECK: simulation.display {{.*}} newline = true radix = <octal>
// CHECK: simulation.display {{.*}} newline = true radix = <hex>
// CHECK: simulation.display {{.*}} newline = false radix = <decimal>
// CHECK: simulation.display {{.*}} newline = false radix = <binary>
// CHECK: simulation.display {{.*}} newline = false radix = <octal>
// CHECK: simulation.display {{.*}} newline = false radix = <hex>
// CHECK: simulation.file.open_mcd {{.*}}, %[[OUT_PATH]]
// CHECK: simulation.file.open {{.*}}, %[[INPUT_PATH]], %[[READ_MODE]]
// CHECK: simulation.display {{.*}} newline = true radix = <decimal>
// CHECK: simulation.display {{.*}} newline = true radix = <binary>
// CHECK: simulation.display {{.*}} newline = true radix = <octal>
// CHECK: simulation.display {{.*}} newline = true radix = <hex>
// CHECK: simulation.display {{.*}} newline = false radix = <decimal>
// CHECK: simulation.display {{.*}} newline = false radix = <binary>
// CHECK: simulation.display {{.*}} newline = false radix = <octal>
// CHECK: simulation.display {{.*}} newline = false radix = <hex>
// CHECK: simulation.file.getc
// CHECK: simulation.file.ungetc
// CHECK: %[[LINE_DATA:.*]], %[[LINE_COUNT:.*]] = simulation.file.getline
// CHECK: %[[LINE_LOGIC:.*]] = simulation.logic.from_bits %[[LINE_DATA]]
// CHECK: %[[LINE_VALUE:.*]] = simulation.packed.unflatten %[[LINE_LOGIC]]
// CHECK: simulation.ref.store %[[LINE_VALUE]]
// CHECK: simulation.logic.from_bits %[[LINE_COUNT]]
// CHECK: %[[READ_DATA:.*]], %[[READ_COUNT:.*]] = simulation.file.read_packed
// CHECK: %[[READ_OLD:.*]] = simulation.ref.load
// CHECK: %[[READ_OLD_LOGIC:.*]] = simulation.packed.flatten %[[READ_OLD]]
// CHECK: %[[READ_LOGIC:.*]] = simulation.logic.from_bits %[[READ_DATA]]
// CHECK: %[[READ_PART:.*]] = simulation.logic.dyn_extract %[[READ_LOGIC]]
// CHECK: %[[READ_MERGED:.*]] = simulation.logic.dyn_insert %[[READ_PART]] into %[[READ_OLD_LOGIC]]
// CHECK: %[[READ_VALUE:.*]] = simulation.packed.unflatten %[[READ_MERGED]]
// CHECK: simulation.ref.store %[[READ_VALUE]]
// CHECK: simulation.logic.from_bits %[[READ_COUNT]]
// CHECK: simulation.file.eof
// CHECK: simulation.file.seek
// CHECK: simulation.file.tell
// CHECK: simulation.file.rewind
// CHECK: simulation.file.flush
// CHECK: simulation.file.flush {{.*}}, %[[ALL_FILES]]
// CHECK: simulation.file.close
// CHECK: simulation.file.close

`timescale 1ns / 1ps
module system_io;
  integer fd;
  integer mcd;
  integer code;
  integer c;
  reg [79:0] line;
  reg [79:0] data;
  initial begin
    $display("value=%0h", data);
    $display("%m %l", , data);
    begin : named
      $display("%m %l %0t", 2);
    end
    $displayb(data);
    $displayo(data);
    $displayh(data);
    $write(data);
    $writeb(data);
    $writeo(data);
    $writeh(data);
    mcd = integer'($fopen(string'("out.log")));
    fd = integer'($fopen(string'("input.bin"), string'("rb")));
    $fdisplay(fd, data);
    $fdisplayb(fd, data);
    $fdisplayo(fd, data);
    $fdisplayh(fd, "value=", data);
    $fwrite(fd, data);
    $fwriteb(fd, data);
    $fwriteo(fd, data);
    $fwriteh(fd, data);
    code = integer'($fgetc(int'(fd)));
    code = integer'($ungetc(int'(c), int'(fd)));
    code = $fgets(line, fd);
    code = $fread(data, fd);
    code = integer'($feof(int'(fd)));
    code = integer'($fseek(int'(fd), 1, 0));
    code = integer'($ftell(int'(fd)));
    code = integer'($rewind(int'(fd)));
    $fflush(int'(fd));
    $fflush;
    $fclose(int'(fd));
    $fclose(int'(mcd));
  end
endmodule
