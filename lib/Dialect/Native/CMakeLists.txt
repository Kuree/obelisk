add_mlir_library(ObeliskNativeIR
  NativeDialect.cpp

  DEPENDS
  ObeliskNativeAttrsIncGen
  ObeliskNativeDialectIncGen

  LINK_LIBS PUBLIC
  MLIRIR
)
