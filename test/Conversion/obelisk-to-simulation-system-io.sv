// RUN: obelisk -emit-sim %s | FileCheck %s

module system_io;
  timeunit 1ns;
  timeprecision 1ps;
  integer fd, mcd, code, c;
  reg [79:0] line, data;

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
    mcd = $fopen("out.log");
    fd = $fopen("input.bin", "rb");
    $fdisplay(fd, data);
    $fdisplayb(fd, data);
    $fdisplayo(fd, data);
    $fdisplayh(fd, "value=", data);
    $fwrite(fd, data);
    $fwriteb(fd, data);
    $fwriteo(fd, data);
    $fwriteh(fd, data);
    code = $fgetc(fd);
    code = $ungetc(c, fd);
    code = $fgets(line, fd);
    code = $fread(data, fd);
    code = $feof(fd);
    code = $fseek(fd, 1, 0);
    code = $ftell(fd);
    code = $rewind(fd);
    $fflush(fd);
    $fflush();
    $fclose(fd);
    $fclose(mcd);
  end
endmodule

// CHECK-DAG: %[[FORMAT:.*]] = obelisk_sim.bytes.constant "value=%0h"
// CHECK-DAG: %[[STDOUT:.*]] = arith.constant 1 : i32
// CHECK-DAG: %[[OUT_PATH:.*]] = obelisk_sim.bytes.constant "out.log"
// CHECK-DAG: %[[INPUT_PATH:.*]] = obelisk_sim.bytes.constant "input.bin"
// CHECK-DAG: %[[READ_MODE:.*]] = obelisk_sim.bytes.constant "rb"
// CHECK-DAG: obelisk_sim.bytes.constant "value="
// CHECK-DAG: %[[ALL_FILES:.*]] = arith.constant 0 : i32
// CHECK: obelisk_sim.display {{.*}} to %[[STDOUT]](%[[FORMAT]], {{.*}}) newline = true radix = 10 flags = [0, 0] {library_cell = "work.system_io", scope = "system_io", time_multiplier = 1000 : i64}
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 10 flags = [0, 2, 0] {library_cell = "work.system_io", scope = "system_io", time_multiplier = 1000 : i64}
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 10 flags = [0, 1] {library_cell = "work.system_io", scope = "system_io.named", time_multiplier = 1000 : i64}
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 16
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 10
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 16
// CHECK: obelisk_sim.file.open_mcd {{.*}}, %[[OUT_PATH]]
// CHECK: obelisk_sim.file.open {{.*}}, %[[INPUT_PATH]], %[[READ_MODE]]
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 10
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = true radix = 16
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 10
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 2
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 8
// CHECK: obelisk_sim.display {{.*}} newline = false radix = 16
// CHECK: obelisk_sim.file.getc
// CHECK: obelisk_sim.file.ungetc
// CHECK: %[[LINE_DATA:.*]], %[[LINE_COUNT:.*]] = obelisk_sim.file.getline
// CHECK: %[[LINE_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[LINE_DATA]]
// CHECK: %[[LINE_VALUE:.*]] = obelisk_sim.packed.unflatten %[[LINE_LOGIC]]
// CHECK: obelisk_sim.ref.store %[[LINE_VALUE]]
// CHECK: obelisk_sim.logic.from_bits %[[LINE_COUNT]]
// CHECK: %[[READ_DATA:.*]], %[[READ_COUNT:.*]] = obelisk_sim.file.read_packed
// CHECK: %[[READ_LOGIC:.*]] = obelisk_sim.logic.from_bits %[[READ_DATA]]
// CHECK: %[[READ_VALUE:.*]] = obelisk_sim.packed.unflatten %[[READ_LOGIC]]
// CHECK: obelisk_sim.ref.store %[[READ_VALUE]]
// CHECK: obelisk_sim.logic.from_bits %[[READ_COUNT]]
// CHECK: obelisk_sim.file.eof
// CHECK: obelisk_sim.file.seek
// CHECK: obelisk_sim.file.tell
// CHECK: obelisk_sim.file.rewind
// CHECK: obelisk_sim.file.flush
// CHECK: obelisk_sim.file.flush {{.*}}, %[[ALL_FILES]]
// CHECK: obelisk_sim.file.close
// CHECK: obelisk_sim.file.close
