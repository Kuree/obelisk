# Clause 34 providers are user callbacks and may throw. Keep exception cleanup
# enabled in this boundary translation unit so cleartext RAII remains effective.
set_source_files_properties(Frontend.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions")

add_mlir_library(ObeliskFrontend
  Frontend.cpp
  SDF.cpp
  Tokens.cpp

  DEPENDS
  SlangEnumsIncGen
  SlangOpsIncGen
  ObeliskVPIObjectModelIncGen
  check-slang-ast-inventory

  LINK_LIBS PUBLIC
  ObeliskSDFIR
  ObeliskSlangIR
  MLIRIR
  MLIRSupport
  slang::slang
)
target_compile_features(obj.ObeliskFrontend PRIVATE cxx_std_20)
# slang v11.0's vendored Boost mode is part of its public header ABI, but the
# definition is not propagated through add_mlir_library's object target.
if(TARGET boost_unordered)
  target_compile_definitions(obj.ObeliskFrontend
    PRIVATE SLANG_BOOST_SINGLE_HEADER)
endif()
