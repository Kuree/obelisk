add_executable(talos-filecheck FileCheck.cpp)
set_target_properties(talos-filecheck PROPERTIES OUTPUT_NAME FileCheck)
target_link_libraries(talos-filecheck PRIVATE
  LLVMFileCheck
  LLVMSupport
)

add_executable(talos-not Not.cpp)
set_target_properties(talos-not PROPERTIES OUTPUT_NAME not)
target_link_libraries(talos-not PRIVATE LLVMSupport)
