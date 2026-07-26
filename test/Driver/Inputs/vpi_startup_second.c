#include "vpi_user.h"

static void startup(void) { vpi_printf("startup second\n"); }

void (*vlog_startup_routines[])(void) = {startup, 0};
