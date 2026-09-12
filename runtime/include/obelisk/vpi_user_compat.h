//===- vpi_user_compat.h - Obelisk IEEE 1800-2023 additions -----*- C -*-===//
//
// Public compatibility definitions missing from the vendored IEEE headers.
// Obelisk's staged sv_vpi_user.h includes this file automatically.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_VPI_USER_COMPAT_H
#define OBELISK_VPI_USER_COMPAT_H

// Figures 37-10 and 37-85 name this one-to-many iterator.  The 729 slot is
// vacant immediately before vpiMethods (730) in the published header.
#ifndef vpiNetTypedef
#define vpiNetTypedef 729
#endif

// IEEE 1800-2023 net-type property values from clause 37.16.
#ifndef vpiNettypeNet
#define vpiNettypeNet 14
#endif
#ifndef vpiNettypeNetSelect
#define vpiNettypeNetSelect 15
#endif
#ifndef vpiInterconnect
#define vpiInterconnect 16
#endif

#endif // OBELISK_VPI_USER_COMPAT_H
