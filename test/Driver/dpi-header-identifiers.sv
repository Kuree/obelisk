// RUN: %obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s < %t.h
// RUN: %llvm_dist/bin/clang -x c -fsyntax-only -include %t.h \
// RUN:   -I%resource_dir/include /dev/null
// RUN: %llvm_dist/bin/clang++ -std=c++17 -x c++ -fsyntax-only -include %t.h \
// RUN:   -I%resource_dir/include /dev/null

module dpi_header_identifiers;
  typedef struct {
    int \int ;
    int \class ;
    int \namespace ;
    int \operator ;
    int \a-b ;
    int a_b;
  } \struct ;

  import "DPI-C" function void consume_identifiers(input \struct  value);
  import "DPI-C" dpi_header_identifiers = function void collision();
endmodule

// CHECK: typedef struct dpi_header_identifiers_1 {
// CHECK: int32_t obelisk_int;
// CHECK: int32_t obelisk_class;
// CHECK: int32_t obelisk_namespace;
// CHECK: int32_t obelisk_operator;
// CHECK: int32_t a_b;
// CHECK: int32_t a_b_1;
// CHECK: } dpi_header_identifiers_1;
// CHECK: void consume_identifiers(const dpi_header_identifiers_1 *arg0);
// CHECK: void dpi_header_identifiers(void);
