//===- Options.cpp - Obelisk compiler driver option table ----------------===//

#include "Options.h"

#include "llvm/Option/Option.h"

using namespace obelisk::driver::options;
using namespace llvm::opt;

#define OPTTABLE_STR_TABLE_CODE
#include "Options.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "Options.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

#define OPTTABLE_PREFIXES_UNION_CODE
#include "Options.inc"
#undef OPTTABLE_PREFIXES_UNION_CODE

static constexpr OptTable::Info optionInfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Options.inc"
#undef OPTION
};

namespace {

class ObeliskOptTable : public PrecomputedOptTable {
public:
  ObeliskOptTable()
      : PrecomputedOptTable(OptionStrTable, OptionPrefixesTable,
                            optionInfoTable, OptionPrefixesUnion) {}
};

} // namespace

const OptTable &obelisk::driver::getDriverOptTable() {
  static ObeliskOptTable table;
  return table;
}
