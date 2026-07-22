# Provision a pinned Debian target sysroot without consulting host tools.
#
# This script is deliberately invoked at build time.  Its only durable success
# indicator is STAMP; every download, extraction and assembled sysroot is
# prepared under a temporary name and renamed before STAMP is written.

cmake_minimum_required(VERSION 3.20)

foreach(required STAMP SYSROOT TARGET_TRIPLE LAYOUT_VERSION PACKAGE_CACHE
                 PACKAGE0_NAME PACKAGE0_URL PACKAGE0_SHA256
                 PACKAGE1_NAME PACKAGE1_URL PACKAGE1_SHA256
                 PACKAGE2_NAME PACKAGE2_URL PACKAGE2_SHA256)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "ProvisionTargetSysroot.cmake: ${required} is required")
  endif()
endforeach()

if(NOT TARGET_TRIPLE STREQUAL "x86_64-unknown-linux-gnu")
  message(FATAL_ERROR "unsupported target triple: ${TARGET_TRIPLE}")
endif()

function(obelisk_resolve_sysroot_path root path output)
  get_filename_component(root "${root}" ABSOLUTE)
  get_filename_component(current "${root}/${path}" ABSOLUTE)
  foreach(depth RANGE 0 40)
    file(RELATIVE_PATH relative "${root}" "${current}")
    if(IS_ABSOLUTE "${relative}" OR relative MATCHES "^\\.\\.(/|$)")
      message(FATAL_ERROR
        "target sysroot validation failed: ${path} escapes ${root}")
    endif()
    if(NOT IS_SYMLINK "${current}")
      if(NOT EXISTS "${current}")
        message(FATAL_ERROR
          "target sysroot validation failed: dangling ${path}")
      endif()
      set(${output} "${current}" PARENT_SCOPE)
      return()
    endif()
    file(READ_SYMLINK "${current}" target)
    if(IS_ABSOLUTE "${target}")
      set(current "${root}${target}")
    else()
      get_filename_component(parent "${current}" DIRECTORY)
      set(current "${parent}/${target}")
    endif()
    get_filename_component(current "${current}" ABSOLUTE)
  endforeach()
  message(FATAL_ERROR
    "target sysroot validation failed: symlink loop at ${root}/${path}")
endfunction()

function(obelisk_validate_sysroot root)
  set(required_files
    usr/include/stdio.h
    usr/include/stdlib.h
    usr/include/x86_64-linux-gnu/bits/libc-header-start.h
    usr/include/linux/version.h
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
    lib/x86_64-linux-gnu/libm.so.6
    lib/x86_64-linux-gnu/libmvec.so.1
    lib/x86_64-linux-gnu/libmvec-2.28.so
    lib/x86_64-linux-gnu/libpthread.so.0
    lib/x86_64-linux-gnu/libdl.so.2
    lib/x86_64-linux-gnu/librt.so.1
    lib/x86_64-linux-gnu/ld-2.28.so
    lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
    usr/share/doc/libc6/copyright)
  foreach(path IN LISTS required_files)
    obelisk_resolve_sysroot_path("${root}" "${path}" resolved)
    if(IS_DIRECTORY "${resolved}")
      message(FATAL_ERROR
        "target sysroot validation failed: ${path} is not a file")
    endif()
  endforeach()
  obelisk_resolve_sysroot_path(
    "${root}" "lib64/ld-linux-x86-64.so.2" resolved)
  if(IS_DIRECTORY "${resolved}")
    message(FATAL_ERROR
      "target sysroot validation failed: lib64/ld-linux-x86-64.so.2 is not a file")
  endif()
  file(GLOB_RECURSE entries LIST_DIRECTORIES TRUE RELATIVE "${root}"
       "${root}/*")
  foreach(path IN LISTS entries)
    if(IS_SYMLINK "${root}/${path}")
      obelisk_resolve_sysroot_path("${root}" "${path}" resolved)
    endif()
  endforeach()
endfunction()

# A caller-provided tree is never copied or modified.  It is validated on the
# first build for this content key, then represented by the build-owned stamp.
if(DEFINED PREEXTRACTED_SYSROOT AND NOT PREEXTRACTED_SYSROOT STREQUAL "")
  get_filename_component(PREEXTRACTED_SYSROOT "${PREEXTRACTED_SYSROOT}"
                         ABSOLUTE)
  obelisk_validate_sysroot("${PREEXTRACTED_SYSROOT}")
  get_filename_component(stamp_dir "${STAMP}" DIRECTORY)
  file(MAKE_DIRECTORY "${stamp_dir}")
  set(stamp_tmp "${STAMP}.tmp")
  file(WRITE "${stamp_tmp}"
    "layout=${LAYOUT_VERSION}\n"
    "target=${TARGET_TRIPLE}\n"
    "source=${PREEXTRACTED_SYSROOT}\n"
    "${PACKAGE0_NAME}=${PACKAGE0_SHA256}\n"
    "${PACKAGE1_NAME}=${PACKAGE1_SHA256}\n"
    "${PACKAGE2_NAME}=${PACKAGE2_SHA256}\n")
  file(RENAME "${stamp_tmp}" "${STAMP}")
  return()
endif()

file(MAKE_DIRECTORY "${PACKAGE_CACHE}")
get_filename_component(work_root "${SYSROOT}" DIRECTORY)
file(MAKE_DIRECTORY "${work_root}/packages")

foreach(index RANGE 0 2)
  set(name "${PACKAGE${index}_NAME}")
  set(url "${PACKAGE${index}_URL}")
  set(expected "${PACKAGE${index}_SHA256}")
  set(archive "${PACKAGE_CACHE}/${name}")

  set(reuse FALSE)
  if(EXISTS "${archive}")
    file(SHA256 "${archive}" actual)
    if(actual STREQUAL expected)
      set(reuse TRUE)
    else()
      # A bad cache entry must never be mistaken for a successful provision.
      file(REMOVE "${archive}")
    endif()
  endif()
  if(NOT reuse)
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
    set(download "${archive}.tmp-${nonce}")
    file(DOWNLOAD "${url}" "${download}"
      STATUS status
      SHOW_PROGRESS
      TLS_VERIFY ON)
    list(GET status 0 code)
    list(GET status 1 message_text)
    if(NOT code EQUAL 0)
      file(REMOVE "${download}")
      message(FATAL_ERROR "failed to download ${name}: ${message_text}")
    endif()
    file(SHA256 "${download}" actual)
    if(NOT actual STREQUAL expected)
      file(REMOVE "${download}")
      message(FATAL_ERROR "downloaded ${name} failed its SHA-256 check")
    endif()
    file(RENAME "${download}" "${archive}")
  endif()

  set(extract "${work_root}/packages/${name}-${expected}")
  set(extract_stamp "${extract}/.complete")
  if(NOT EXISTS "${extract_stamp}")
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
    set(extract_tmp "${extract}.tmp-${nonce}")
    file(REMOVE_RECURSE "${extract_tmp}")
    file(MAKE_DIRECTORY "${extract_tmp}/ar" "${extract_tmp}/root")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${extract_tmp}/ar")
    file(GLOB data_archives "${extract_tmp}/ar/data.tar*")
    list(LENGTH data_archives data_count)
    if(NOT data_count EQUAL 1)
      file(REMOVE_RECURSE "${extract_tmp}")
      message(FATAL_ERROR "${name} did not contain exactly one data.tar archive")
    endif()
    list(GET data_archives 0 data_archive)
    file(ARCHIVE_EXTRACT INPUT "${data_archive}"
                         DESTINATION "${extract_tmp}/root")
    file(WRITE "${extract_tmp}/.complete" "${expected}\n")
    if(EXISTS "${extract}")
      file(REMOVE_RECURSE "${extract}")
    endif()
    file(RENAME "${extract_tmp}" "${extract}")
  endif()
  set(PACKAGE${index}_ROOT "${extract}/root")
endforeach()

if(NOT EXISTS "${STAMP}")
  string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
  set(sysroot_tmp "${SYSROOT}.tmp-${nonce}")
  file(REMOVE_RECURSE "${sysroot_tmp}")
  file(MAKE_DIRECTORY "${sysroot_tmp}")
  # Debian's development packages intentionally overlay libc6.  Copy in
  # dependency order and retain package documentation in the resulting tree.
  foreach(index RANGE 0 2)
    file(COPY "${PACKAGE${index}_ROOT}/" DESTINATION "${sysroot_tmp}")
  endforeach()
  obelisk_validate_sysroot("${sysroot_tmp}")
  if(EXISTS "${SYSROOT}")
    file(REMOVE_RECURSE "${SYSROOT}")
  endif()
  file(RENAME "${sysroot_tmp}" "${SYSROOT}")
endif()

obelisk_validate_sysroot("${SYSROOT}")
get_filename_component(stamp_dir "${STAMP}" DIRECTORY)
file(MAKE_DIRECTORY "${stamp_dir}")
set(stamp_tmp "${STAMP}.tmp")
file(WRITE "${stamp_tmp}"
  "layout=${LAYOUT_VERSION}\n"
  "target=${TARGET_TRIPLE}\n"
  "${PACKAGE0_NAME}=${PACKAGE0_SHA256}\n"
  "${PACKAGE1_NAME}=${PACKAGE1_SHA256}\n"
  "${PACKAGE2_NAME}=${PACKAGE2_SHA256}\n")
file(RENAME "${stamp_tmp}" "${STAMP}")
