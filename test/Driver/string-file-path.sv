// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

module string_file_path;
  initial begin
    string path;
    string mode;
    integer descriptor;
    integer first;
    integer second;
    integer count;
    string line;

    path = "obelisk-managed-string-file.tmp";
    mode = "w";
    descriptor = $fopen(path, mode);
    $fwrite(descriptor, "OK\nTAIL");
    $fclose(descriptor);

    mode = "r";
    descriptor = $fopen(path, mode);
    first = $fgetc(descriptor);
    second = $fgetc(descriptor);
    $fclose(descriptor);
    $display("file=%c%c", first, second);

    descriptor = $fopen(path, mode);
    count = $fgets(line, descriptor);
    $display("first-line=%0d:%c%c", count, line.getc(0), line.getc(1));
    count = $fgets(line, descriptor);
    $display("second-line=%0d:%s", count, line);
    $fclose(descriptor);
  end
endmodule

// CHECK: file=OK
// CHECK-NEXT: first-line=3:OK
// CHECK-NEXT: second-line=4:TAIL
