//===- VPIInternal.h - Canonical IEEE VPI declarations ----------*- C++ -*-===//
//
// The runtime and external VPI modules must agree on every structure layout,
// constant, and function prototype. Use Slang's vendored IEEE header here as
// well as in the staged SDK instead of maintaining a second, partial copy.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_RUNTIME_VPI_INTERNAL_H
#define OBELISK_RUNTIME_VPI_INTERNAL_H

// The runtime defines the canonical entry points.  On Windows the public
// header otherwise declares them dllimport, which conflicts with those
// definitions; the definitions themselves carry OBELISK_VPI_EXPORT.
#if defined(_WIN32)
#define PLI_DLLISPEC
#define PLI_DLLESPEC
#endif

#include "vpi_user.h"
#include "sv_vpi_user.h"

#endif
