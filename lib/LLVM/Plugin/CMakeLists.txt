add_library(ObeliskLLVMPassPlugin MODULE
  PassPlugin.cpp
  ../ClockedGuardLayout.cpp
)
llvm_update_compile_flags(ObeliskLLVMPassPlugin)
target_include_directories(ObeliskLLVMPassPlugin PRIVATE
  ${PROJECT_SOURCE_DIR}/include ${LLVM_INCLUDE_DIRS})
set_target_properties(ObeliskLLVMPassPlugin PROPERTIES
  PREFIX ""
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
