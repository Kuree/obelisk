add_mlir_library(TalosNativeIR
  NativeDialect.cpp

  DEPENDS
  TalosNativeAttrsIncGen
  TalosNativeDialectIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
