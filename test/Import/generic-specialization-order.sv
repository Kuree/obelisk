// RUN: obelisk -emit-slang %s -o %t.first.mlir
// RUN: obelisk -emit-slang %s -o %t.second.mlir
// RUN: diff %t.first.mlir %t.second.mlir
// RUN: FileCheck %s < %t.first.mlir

package p1;
  class T;
  endclass
endpackage

package p2;
  class T;
  endclass
endpackage

class G #(type P = int);
  P value;
endclass

module top;
  G#(p1::T) a;
  G#(p2::T) b;
endmodule

// Slang's generic specialization map is hash-ordered, and its ordinary type
// printer spells both nominal parameters as just "T". The importer must use
// the package-qualified nominal identity to produce stable specialization IDs.
// CHECK-LABEL: slang.symbol.generic_class_def
// CHECK: slang.type.class_type
// CHECK: slang.symbol.type_parameter {{.*}}semantic_type = !slang.class_handle<{{[^>]*}}.p1::{{[^>]*}}.T>
// CHECK: slang.type.class_type
// CHECK: slang.symbol.type_parameter {{.*}}semantic_type = !slang.class_handle<{{[^>]*}}.p2::{{[^>]*}}.T>
