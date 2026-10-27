set(LLVM_TARGET_DEFINITIONS Passes.td)
mlir_tablegen(Passes.h.inc -gen-pass-decls -name TalosConversion)
add_public_tablegen_target(TalosConversionPassIncGen)
