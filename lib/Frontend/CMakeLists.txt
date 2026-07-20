add_mlir_library(ObeliskFrontend
  Frontend.cpp

  LINK_LIBS PUBLIC
  CIRCTImportVerilog
  MLIRIR
  MLIRSupport
)
