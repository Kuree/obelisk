# LLVM IR passes run on the generated design during optimization and codegen.
# The static library goes into the driver; Plugin/ builds the same sources as a
# module loaded into `opt` (-load-pass-plugin) so the passes are tested on LLVM
# IR directly.
add_llvm_library(ObeliskLLVMPasses STATIC
  ClockedGuardLayout.cpp
  WideIntegerVectorization.cpp

  LINK_COMPONENTS
  Analysis
  Core
  Passes
  Support
  TransformUtils
)
target_include_directories(ObeliskLLVMPasses PUBLIC
  ${PROJECT_SOURCE_DIR}/include)

if(NOT WIN32)
  add_subdirectory(Plugin)
endif()
