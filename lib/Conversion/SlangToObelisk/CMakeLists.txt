add_mlir_library(ObeliskSlangToObelisk
  SlangToObelisk.cpp

  DEPENDS
  ObeliskConversionPassIncGen
  ObeliskASTOpsIncGen
  SlangOpsIncGen

  LINK_LIBS PUBLIC
  ObeliskIR
  ObeliskSlangIR
  MLIRIR
  MLIRPass
  MLIRTransformUtils
)
