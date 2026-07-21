add_executable(obelisk-filecheck FileCheck.cpp)
set_target_properties(obelisk-filecheck PROPERTIES OUTPUT_NAME FileCheck)
target_link_libraries(obelisk-filecheck PRIVATE
  LLVMFileCheck
  LLVMSupport
)

add_executable(obelisk-not Not.cpp)
set_target_properties(obelisk-not PROPERTIES OUTPUT_NAME not)
target_link_libraries(obelisk-not PRIVATE LLVMSupport)
