if(NOT CMAKE_CROSSCOMPILING)
  add_tablegen(talos-tblgen TALOS
    talos-tblgen.cpp
  )
  llvm_update_compile_flags(talos-tblgen)
  target_link_libraries(talos-tblgen PRIVATE MLIRTblgenLib)
endif()
