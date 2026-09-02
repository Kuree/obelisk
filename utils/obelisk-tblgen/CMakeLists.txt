if(NOT CMAKE_CROSSCOMPILING)
  add_tablegen(obelisk-tblgen OBELISK
    obelisk-tblgen.cpp
  )
  llvm_update_compile_flags(obelisk-tblgen)
  target_link_libraries(obelisk-tblgen PRIVATE MLIRTblgenLib)
endif()
