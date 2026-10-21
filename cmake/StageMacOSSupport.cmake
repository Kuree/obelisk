cmake_minimum_required(VERSION 3.20)

foreach(input SDK DESTINATION RUNTIME RUNTIME_LTO SOURCE_DIR LLVM_NM)
  if(NOT DEFINED ${input} OR "${${input}}" STREQUAL "")
    message(FATAL_ERROR "Missing ${input} for macOS link-support staging")
  endif()
endforeach()
foreach(library libSystem.tbd libc++.tbd)
  if(NOT EXISTS "${SDK}/usr/lib/${library}")
    message(FATAL_ERROR "The macOS SDK is missing usr/lib/${library}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${DESTINATION}/sdk/usr/lib")
# Preserve the SDK's relative stub aliases and re-export closure. No headers,
# binary system libraries, or build tools are needed at simulation-link time.
file(COPY "${SDK}/usr/lib/" DESTINATION "${DESTINATION}/sdk/usr/lib"
  FILES_MATCHING PATTERN "*.tbd")
file(COPY "${RUNTIME}" "${RUNTIME_LTO}" DESTINATION "${DESTINATION}")
set(OUTPUT_PREFIX "${DESTINATION}/weak-imports")
include("${CMAKE_CURRENT_LIST_DIR}/MacOSWeakImports.cmake")
file(MAKE_DIRECTORY "${DESTINATION}/licenses/compiler-rt"
                    "${DESTINATION}/licenses/obelisk")
configure_file("${SOURCE_DIR}/docs/third-party/licenses/compiler-rt-LICENSE.TXT"
  "${DESTINATION}/licenses/compiler-rt/LICENSE.TXT" COPYONLY)
configure_file("${SOURCE_DIR}/docs/third-party/licenses/compiler-rt-CREDITS.TXT"
  "${DESTINATION}/licenses/compiler-rt/CREDITS.TXT" COPYONLY)
configure_file("${SOURCE_DIR}/LICENSE"
  "${DESTINATION}/licenses/obelisk/LICENSE" COPYONLY)
file(WRITE "${DESTINATION}/README.txt"
  "Obelisk macOS ARM64 link support.\n"
  "The runtime and compiler-rt are static; libSystem and libc++ are supplied by macOS.\n"
  "sdk/usr/lib contains only text link stubs from the build's macOS SDK.\n")
file(WRITE "${DESTINATION}/.complete" "macos-arm64\n")
