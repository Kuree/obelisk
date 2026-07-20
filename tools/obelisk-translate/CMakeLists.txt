# obelisk-translate: SystemVerilog -> IR
#
# Stage 1 (now): SystemVerilog -> Moore dialect, by linking CIRCT's slang-based
# ImportVerilog frontend in-process. This is the input side of obelisk's future
# coroutine-lowering pipeline. See DESIGN.md.

get_property(dialect_libs    GLOBAL PROPERTY CIRCT_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY CIRCT_CONVERSION_LIBS)

add_llvm_executable(obelisk-translate
  obelisk-translate.cpp
)

llvm_update_compile_flags(obelisk-translate)

target_link_libraries(obelisk-translate
  PRIVATE
    ${dialect_libs}
    ${conversion_libs}
    CIRCTImportVerilog
    ObeliskIR
    MLIRIR
    MLIRParser
    MLIRSupport
)

mlir_check_all_link_libraries(obelisk-translate)
