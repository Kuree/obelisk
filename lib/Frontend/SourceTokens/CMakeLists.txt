# The slang lexer behind `talos -dump-tokens`, usable without the rest of
# the frontend: its header is C++17 and exposes no slang or MLIR types.
add_library(TalosSourceTokens STATIC
  SourceTokens.cpp
)
target_include_directories(TalosSourceTokens PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)
target_compile_features(TalosSourceTokens PUBLIC cxx_std_17 PRIVATE cxx_std_20)
target_link_libraries(TalosSourceTokens PUBLIC LLVMSupport PRIVATE slang::slang)
if(TARGET boost_unordered)
  target_compile_definitions(TalosSourceTokens
    PRIVATE SLANG_BOOST_SINGLE_HEADER)
endif()
set_target_properties(TalosSourceTokens PROPERTIES
  POSITION_INDEPENDENT_CODE ON)
