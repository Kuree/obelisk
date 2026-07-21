add_mlir_library(ObeliskFrontend
  Frontend.cpp

  DEPENDS
  SlangEnumsIncGen
  SlangOpsIncGen
  check-slang-ast-inventory

  LINK_LIBS PUBLIC
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
