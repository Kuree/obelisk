set(LLVM_TARGET_DEFINITIONS NativeAttrs.td)
mlir_tablegen(NativeAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=native)
mlir_tablegen(NativeAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=native)
add_public_tablegen_target(ObeliskNativeAttrsIncGen)

set(LLVM_TARGET_DEFINITIONS NativeDialect.td)
mlir_tablegen(NativeDialect.h.inc -gen-dialect-decls -dialect=native)
mlir_tablegen(NativeDialect.cpp.inc -gen-dialect-defs -dialect=native)
add_public_tablegen_target(ObeliskNativeDialectIncGen)
