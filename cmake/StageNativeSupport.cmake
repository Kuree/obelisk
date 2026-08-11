cmake_minimum_required(VERSION 3.20)

foreach(required STAMP DESTINATION SYSROOT RUNTIME_ARCHIVE RUNTIME_LTO_ARCHIVE
                 LLVM_DIST SOURCE_DIR STAGE_KEY TARGET_TRIPLE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "StageNativeSupport.cmake: ${required} is required")
  endif()
endforeach()

set(required_sysroot_files
  usr/lib/x86_64-linux-gnu/Scrt1.o
  usr/lib/x86_64-linux-gnu/crti.o
  usr/lib/x86_64-linux-gnu/crtn.o
  usr/lib/x86_64-linux-gnu/libc.so
  usr/lib/x86_64-linux-gnu/libc_nonshared.a
  usr/lib/x86_64-linux-gnu/libm.so
  usr/lib/x86_64-linux-gnu/libmvec_nonshared.a
  usr/lib/x86_64-linux-gnu/libpthread.so
  usr/lib/x86_64-linux-gnu/libdl.so
  usr/lib/x86_64-linux-gnu/librt.so
  lib/x86_64-linux-gnu/libc.so.6
  lib/x86_64-linux-gnu/libc-2.28.so
  lib/x86_64-linux-gnu/libm.so.6
  lib/x86_64-linux-gnu/libm-2.28.so
  lib/x86_64-linux-gnu/libmvec.so.1
  lib/x86_64-linux-gnu/libmvec-2.28.so
  lib/x86_64-linux-gnu/libpthread.so.0
  lib/x86_64-linux-gnu/libpthread-2.28.so
  lib/x86_64-linux-gnu/libdl.so.2
  lib/x86_64-linux-gnu/libdl-2.28.so
  lib/x86_64-linux-gnu/librt.so.1
  lib/x86_64-linux-gnu/librt-2.28.so
  lib/x86_64-linux-gnu/ld-2.28.so
  lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
  lib64/ld-linux-x86-64.so.2
  usr/share/doc/libc6/copyright
  usr/share/doc/libc6-dev/copyright
  usr/share/doc/linux-libc-dev/copyright)
foreach(path IN LISTS required_sysroot_files)
  if(NOT EXISTS "${SYSROOT}/${path}" AND NOT IS_SYMLINK "${SYSROOT}/${path}")
    message(FATAL_ERROR "cannot stage missing target support file: ${path}")
  endif()
endforeach()

set(clang_runtime "${LLVM_DIST}/lib/clang/22/lib/${TARGET_TRIPLE}")
set(cxx_runtime "${LLVM_DIST}/lib/${TARGET_TRIPLE}")
set(required_llvm_files
  "${clang_runtime}/clang_rt.crtbegin.o"
  "${clang_runtime}/clang_rt.crtend.o"
  "${clang_runtime}/libclang_rt.builtins.a"
  "${cxx_runtime}/libc++.a"
  "${cxx_runtime}/libc++abi.a"
  "${cxx_runtime}/libunwind.a"
  "${LLVM_DIST}/include/llvm/Support/LICENSE.TXT")
foreach(path IN LISTS required_llvm_files)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "LLVM distribution is missing native support: ${path}")
  endif()
endforeach()

set(stage_material "stage=${STAGE_KEY};")
foreach(path IN ITEMS
    "${RUNTIME_ARCHIVE}"
    "${RUNTIME_LTO_ARCHIVE}"
    ${required_llvm_files}
    "${SOURCE_DIR}/cmake/StageNativeSupport.cmake"
    "${SOURCE_DIR}/LICENSE"
    "${SOURCE_DIR}/docs/third-party/licenses/LGPL-2.1.txt"
    "${SOURCE_DIR}/docs/third-party/licenses/GPL-2.0.txt"
    "${SOURCE_DIR}/docs/third-party/licenses/Apache-2.0.txt"
    "${SOURCE_DIR}/docs/third-party/licenses/LLVM-exception.txt")
  file(SHA256 "${path}" digest)
  string(APPEND stage_material "input=${digest};")
endforeach()
string(SHA256 stage_content_key "${stage_material}")

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(version_root "${DESTINATION}.versions")
set(published "${version_root}/${STAGE_KEY}-${stage_content_key}")
set(stage "${published}.tmp-${nonce}")
file(REMOVE_RECURSE "${stage}")
file(MAKE_DIRECTORY
  "${stage}/glibc/usr/lib/x86_64-linux-gnu"
  "${stage}/glibc/lib/x86_64-linux-gnu"
  "${stage}/glibc/lib64"
  "${stage}/licenses/glibc"
  "${stage}/licenses/obelisk"
  "${stage}/licenses/llvm")

foreach(path IN LISTS required_sysroot_files)
  if(path STREQUAL "usr/share/doc/libc6/copyright")
    configure_file("${SYSROOT}/${path}"
                   "${stage}/licenses/glibc/libc6-copyright" COPYONLY)
  elseif(path STREQUAL "usr/share/doc/libc6-dev/copyright")
    configure_file("${SYSROOT}/${path}"
                   "${stage}/licenses/glibc/libc6-dev-copyright" COPYONLY)
  elseif(path STREQUAL "usr/share/doc/linux-libc-dev/copyright")
    configure_file("${SYSROOT}/${path}"
                   "${stage}/licenses/glibc/linux-libc-dev-copyright" COPYONLY)
  else()
    get_filename_component(parent "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${stage}/glibc/${parent}")
    file(COPY "${SYSROOT}/${path}" DESTINATION "${stage}/glibc/${parent}")
  endif()
endforeach()

# Make target-root absolute links relocatable inside the staged tree. This is
# equivalent to sysroot-relative resolution, but also keeps build tools from
# treating the curated link byproducts as dangling host links.
file(GLOB_RECURSE staged_glibc_entries LIST_DIRECTORIES TRUE
  "${stage}/glibc/*")
foreach(path IN LISTS staged_glibc_entries)
  if(NOT IS_SYMLINK "${path}")
    continue()
  endif()
  file(READ_SYMLINK "${path}" target)
  if(NOT IS_ABSOLUTE "${target}")
    continue()
  endif()
  set(target_path "${stage}/glibc${target}")
  if(NOT EXISTS "${target_path}" AND NOT IS_SYMLINK "${target_path}")
    message(FATAL_ERROR "staged target link is dangling: ${path} -> ${target}")
  endif()
  get_filename_component(parent "${path}" DIRECTORY)
  file(RELATIVE_PATH relative_target "${parent}" "${target_path}")
  file(REMOVE "${path}")
  file(CREATE_LINK "${relative_target}" "${path}" SYMBOLIC
    RESULT relative_link_result)
  if(NOT relative_link_result STREQUAL "0")
    message(FATAL_ERROR
      "failed to make staged target link relocatable: ${relative_link_result}")
  endif()
endforeach()

file(COPY "${RUNTIME_ARCHIVE}" "${RUNTIME_LTO_ARCHIVE}"
  DESTINATION "${stage}")
file(COPY
  "${clang_runtime}/clang_rt.crtbegin.o"
  "${clang_runtime}/clang_rt.crtend.o"
  "${clang_runtime}/libclang_rt.builtins.a"
  "${cxx_runtime}/libc++.a"
  "${cxx_runtime}/libc++abi.a"
  "${cxx_runtime}/libunwind.a"
  DESTINATION "${stage}")
file(COPY "${LLVM_DIST}/include/llvm/Support/LICENSE.TXT"
  DESTINATION "${stage}/licenses/llvm")
configure_file("${SOURCE_DIR}/LICENSE"
               "${stage}/licenses/obelisk/LICENSE" COPYONLY)
configure_file("${SOURCE_DIR}/docs/third-party/licenses/LGPL-2.1.txt"
               "${stage}/licenses/glibc/LGPL-2.1.txt" COPYONLY)
configure_file("${SOURCE_DIR}/docs/third-party/licenses/GPL-2.0.txt"
               "${stage}/licenses/glibc/GPL-2.0.txt" COPYONLY)
configure_file("${SOURCE_DIR}/docs/third-party/licenses/Apache-2.0.txt"
               "${stage}/licenses/llvm/Apache-2.0.txt" COPYONLY)
configure_file("${SOURCE_DIR}/docs/third-party/licenses/LLVM-exception.txt"
               "${stage}/licenses/llvm/LLVM-exception.txt" COPYONLY)

# Record source-tree prefixes embedded by the prebuilt LLVM runtimes. The
# linker strips exactly these SDK-derived prefixes from the final local ELF;
# discovering them here avoids baking one CI worker path into the compiler.
set(llvm_build_prefixes)
foreach(path IN ITEMS
    "${cxx_runtime}/libc++.a"
    "${cxx_runtime}/libc++abi.a"
    "${cxx_runtime}/libunwind.a"
    "${clang_runtime}/libclang_rt.builtins.a")
  file(STRINGS "${path}" embedded_paths
    REGEX "^/.*/(libcxx|libcxxabi|libunwind|compiler-rt|llvm)/")
  foreach(embedded IN LISTS embedded_paths)
    if(embedded MATCHES "^(/.*)/(libcxx|libcxxabi|libunwind|compiler-rt|llvm)/")
      list(APPEND llvm_build_prefixes "${CMAKE_MATCH_1}")
    endif()
  endforeach()
endforeach()
list(REMOVE_DUPLICATES llvm_build_prefixes)
list(SORT llvm_build_prefixes)
set(prefix_manifest "")
foreach(prefix IN LISTS llvm_build_prefixes)
  string(APPEND prefix_manifest "${prefix}\n")
endforeach()
file(WRITE "${stage}/BUILD_PATH_PREFIXES.txt" "${prefix_manifest}")

file(WRITE "${stage}/README.txt"
  "Local Obelisk native-link support for ${TARGET_TRIPLE}.\n"
  "libobelisk_rt.a contains native ELF objects for -O0 links.\n"
  "libobelisk_rt_lto.a contains LLVM bitcode for -O1 through -O3 Full-LTO links.\n"
  "This tree includes glibc inputs from Debian 10 under their own licenses.\n"
  "See licenses/glibc; redistribution also requires corresponding source compliance.\n"
  "Generated executables dynamically depend on a target glibc compatible with 2.28.\n")
set(complete_contents
  "${TARGET_TRIPLE}\n${STAGE_KEY}\n${stage_content_key}\n")
file(WRITE "${stage}/.complete" "${complete_contents}")

function(obelisk_tree_fingerprint root output)
  file(GLOB_RECURSE entries LIST_DIRECTORIES FALSE RELATIVE "${root}"
    "${root}/*")
  if(EXISTS "${root}/.complete")
    list(APPEND entries ".complete")
  endif()
  list(REMOVE_DUPLICATES entries)
  list(SORT entries)
  set(material "")
  foreach(relative IN LISTS entries)
    set(path "${root}/${relative}")
    if(IS_SYMLINK "${path}")
      file(READ_SYMLINK "${path}" target)
      string(APPEND material "link:${relative}=${target};")
    else()
      file(SHA256 "${path}" digest)
      string(APPEND material "file:${relative}=${digest};")
    endif()
  endforeach()
  string(SHA256 fingerprint "${material}")
  set(${output} "${fingerprint}" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY "${version_root}")
if(EXISTS "${published}" OR IS_SYMLINK "${published}")
  obelisk_tree_fingerprint("${stage}" staged_fingerprint)
  obelisk_tree_fingerprint("${published}" published_fingerprint)
  if(staged_fingerprint STREQUAL published_fingerprint)
    file(REMOVE_RECURSE "${stage}")
  else()
    # Preserve the immutable content-addressed name for healthy trees. A
    # damaged live tree is repaired under a fresh sibling and the public link
    # is switched only after that replacement is complete.
    set(published "${published}-repair-${nonce}")
    file(RENAME "${stage}" "${published}")
  endif()
else()
  file(RENAME "${stage}" "${published}")
endif()
set(link "${DESTINATION}.link-${nonce}")
get_filename_component(destination_name "${DESTINATION}" NAME)
get_filename_component(published_name "${published}" NAME)
set(relative_published
  "${destination_name}.versions/${published_name}")
file(CREATE_LINK "${relative_published}" "${link}" SYMBOLIC
  RESULT link_result)
if(NOT link_result STREQUAL "0")
  message(FATAL_ERROR "failed to create native-support link: ${link_result}")
endif()
if(EXISTS "${DESTINATION}" AND NOT IS_SYMLINK "${DESTINATION}")
  # One-time migration from the original unversioned staging layout.
  file(REMOVE_RECURSE "${DESTINATION}")
endif()
file(RENAME "${link}" "${DESTINATION}")
get_filename_component(stamp_dir "${STAMP}" DIRECTORY)
file(MAKE_DIRECTORY "${stamp_dir}")
file(WRITE "${STAMP}.tmp" "${TARGET_TRIPLE}\n")
file(RENAME "${STAMP}.tmp" "${STAMP}")
