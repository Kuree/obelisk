#include "sv_vpi_user.h"

_Static_assert(vpiNetTypedef == 729,
               "staged SDK has the wrong vpiNetTypedef selector");
_Static_assert(vpiNettypeNet == 14,
               "staged SDK has the wrong vpiNettypeNet value");
_Static_assert(vpiNettypeNetSelect == 15,
               "staged SDK has the wrong vpiNettypeNetSelect value");
_Static_assert(vpiInterconnect == 16,
               "staged SDK has the wrong vpiInterconnect value");

int obelisk_vpi_2023_header_smoke(void) { return vpiNetTypedef; }
