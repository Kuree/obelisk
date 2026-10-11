// Verify the libc++abi layout used by web/wasi.js for JavaScript exceptions.
// This translation unit is a build-only WASM ABI check, not an archive member.
#ifndef __EMSCRIPTEN__
#error "The JavaScript exception ABI check requires Emscripten"
#endif
#ifndef __EMSCRIPTEN_EXCEPTIONS__
#define __EMSCRIPTEN_EXCEPTIONS__ 1
#endif

#include "cxa_exception.h"
#include <cstddef>

using Exception = __cxxabiv1::__cxa_exception;
static_assert(sizeof(Exception) == 24);
static_assert(offsetof(Exception, exceptionType) == 4);
static_assert(offsetof(Exception, exceptionDestructor) == 8);
static_assert(offsetof(Exception, caught) == 12);
static_assert(offsetof(Exception, rethrown) == 13);
static_assert(offsetof(Exception, adjustedPtr) == 16);
