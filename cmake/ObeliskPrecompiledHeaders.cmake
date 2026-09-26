# The generated op declarations and LLVM headers dominate parsing in these
# multi-source libraries. Keep a private PCH per target: sharing one with
# REUSE_FROM would also require identical defines, include paths and flags.
# In particular, do not mix Slang's C++20/exception ABI with MLIR's C++17 ABI.
include_guard(GLOBAL)
option(OBELISK_ENABLE_PCH "Precompile headers for the large compiler libraries" ON)

function(obelisk_precompile_headers library)
  if(NOT OBELISK_ENABLE_PCH OR CMAKE_DISABLE_PRECOMPILE_HEADERS)
    return()
  endif()
  # add_mlir_library compiles through an object library, not the archive target.
  if(TARGET obj.${library})
    set(compile_target obj.${library})
  else()
    set(compile_target ${library})
  endif()
  target_precompile_headers(${compile_target} PRIVATE ${ARGN})
  # Allow GCC's preprocessed output to retain the PCH reference when a compiler
  # launcher (e.g. ccache) preprocesses separately from compilation.
  # ccache additionally requires sloppiness=pch_defines,time_macros.
  target_compile_options(${compile_target} PRIVATE
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Werror=invalid-pch>"
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fpch-preprocess>"
    "$<$<COMPILE_LANG_AND_ID:CXX,Clang,AppleClang>:SHELL:-Xclang -fno-pch-timestamp>")
endfunction()
