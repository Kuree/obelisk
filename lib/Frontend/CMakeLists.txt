add_subdirectory(SourceTokens)

if(WIN32)
  # Slang's expected<> header follows the Microsoft STL exception setting.
  # Match Slang's exception ABI in this isolated frontend library.
  set(LLVM_REQUIRES_EH ON)
endif()

add_mlir_library(TalosFrontend
  Frontend.cpp
  SDF.cpp
  Tokens.cpp

  DEPENDS
  TalosSimulationAttrsIncGen
  TalosSimulationDialectIncGen
  TalosSimulationEnumsIncGen
  TalosSimulationTypesIncGen
  SlangEnumsIncGen
  SlangOpsIncGen
  TalosVPIObjectModelIncGen
  check-slang-ast-inventory

  LINK_LIBS PUBLIC
  TalosSDFIR
  TalosSlangIR
  TalosSourceTokens
  MLIRIR
  MLIRSupport
  slang::slang
)
target_compile_features(obj.TalosFrontend PRIVATE cxx_std_20)
# slang v11.0's vendored Boost mode is part of its public header ABI, but the
# definition is not propagated through add_mlir_library's object target.
if(TARGET boost_unordered)
  target_compile_definitions(obj.TalosFrontend
    PRIVATE SLANG_BOOST_SINGLE_HEADER)
endif()
