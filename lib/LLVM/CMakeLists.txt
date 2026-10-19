# LLVM IR passes run on the generated design after the optimization pipeline.
# The static library goes into the driver; Plugin/ builds the same sources as a
# module loaded into `opt` (-load-pass-plugin) so the passes are tested on LLVM
# IR directly.
add_llvm_library(ObeliskLLVMPasses STATIC
  ClockedGuardLayout.cpp

  LINK_COMPONENTS
  Core
  Passes
  Support
)
target_include_directories(ObeliskLLVMPasses PUBLIC
  ${PROJECT_SOURCE_DIR}/include)

add_subdirectory(Plugin)
