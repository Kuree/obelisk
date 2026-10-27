//===- Options.cpp - Talos compiler driver option table ----------------===//

#include "Options.h"

#include "llvm/Option/Option.h"

using namespace talos::driver::options;
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

class TalosOptTable : public PrecomputedOptTable {
public:
  TalosOptTable()
      : PrecomputedOptTable(OptionStrTable, OptionPrefixesTable,
                            optionInfoTable, OptionPrefixesUnion) {}
};

} // namespace

const OptTable &talos::driver::getDriverOptTable() {
  static TalosOptTable table;
  return table;
}
