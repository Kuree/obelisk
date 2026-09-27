#ifndef OBELISK_DIALECT_SCHEDULE_DIALECT_TD
#define OBELISK_DIALECT_SCHEDULE_DIALECT_TD
include "mlir/IR/DialectBase.td"
def ScheduleDialect : Dialect {
  let name = "schedule";
  let cppNamespace = "::obelisk::schedule";
  let summary = "Verified simulation execution plans";
  let description = [{
    Describes effects, activation and ordering dependencies, continuation and
    timing sites, commit groups, convergence, kernel ownership and execution
    specialization. Simulation-to-schedule derives these plans; scheduling
    transforms optimize them; executor conversions consume them.
  }];
  let useDefaultAttributePrinterParser = 1;
}
#endif
