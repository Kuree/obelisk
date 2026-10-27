add_library(TalosLLVMPassPlugin MODULE
  PassPlugin.cpp
  ../ClockedGuardLayout.cpp
  ../WideIntegerVectorization.cpp
)
llvm_update_compile_flags(TalosLLVMPassPlugin)
target_include_directories(TalosLLVMPassPlugin PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${LLVM_INCLUDE_DIRS})
set_target_properties(TalosLLVMPassPlugin PROPERTIES
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
