//===- NativeSizeCensusTestPass.cpp - Print generated-code size census ----===//
//
// Count the operations of a lowered native module by symbol family: function
// bodies, and global initializers with the number of distinct contents. Names
// are folded into families by replacing user-named dot segments with `*` and
// numeric suffixes with `N`, so `top.u.proc_3.__talos_eval_body_7` and
// `p.__talos_eval_body_2` are both `*.__talos_eval_body_N`.
//
//===----------------------------------------------------------------------===//

#include "AnalysisTestPasses.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <vector>

using namespace mlir;

namespace {

/// Folds one dot segment: digit runs that start the segment or follow `_`
/// and end it or precede `_` become `N`.
std::string foldSegment(StringRef segment) {
  std::string folded;
  for (size_t index = 0; index < segment.size();) {
    bool boundary = index == 0 || segment[index - 1] == '_';
    size_t end = index;
    while (end < segment.size() && llvm::isDigit(segment[end]))
      ++end;
    if (boundary && end != index &&
        (end == segment.size() || segment[end] == '_')) {
      folded += 'N';
      index = end;
      continue;
    }
    folded += segment[index++];
  }
  return folded;
}

/// The family of a symbol name. Generated segments start with `__talos`;
/// every run of other segments of a dotted name becomes one `*`.
std::string familyOf(StringRef name) {
  SmallVector<StringRef> segments;
  name.split(segments, '.');
  if (segments.size() == 1)
    return foldSegment(name);
  std::string family;
  bool user = false;
  for (StringRef segment : segments) {
    if (!segment.starts_with("__talos")) {
      if (!user)
        family += family.empty() ? "*" : ".*";
      user = true;
      continue;
    }
    if (!family.empty())
      family += '.';
    family += foldSegment(segment);
    user = false;
  }
  return family;
}

/// A structural key of an initializer region: per op, its name, attribute
/// dictionary, result types and the walk positions of its operands' producers
/// (or block arguments), so chains that look alike but are wired differently
/// stay distinct. Attributes (inherent ones included) and types are uniqued,
/// so their storage pointers identify contents. `ignoreAddresses` drops the
/// symbol of `llvm.mlir.addressof`.
std::vector<uintptr_t> contentKey(Region &region, bool ignoreAddresses) {
  std::vector<uintptr_t> key;
  DenseMap<Value, uintptr_t> positions;
  uintptr_t next = 0;
  for (BlockArgument argument : region.getArguments())
    positions[argument] = next++;
  region.walk([&](Operation *op) {
    key.push_back(
        reinterpret_cast<uintptr_t>(op->getName().getAsOpaquePointer()));
    if (ignoreAddresses && isa<LLVM::AddressOfOp>(op))
      key.push_back(0);
    else
      key.push_back(reinterpret_cast<uintptr_t>(
          op->getAttrDictionary().getAsOpaquePointer()));
    key.push_back(op->getNumOperands());
    for (Value operand : op->getOperands()) {
      auto position = positions.find(operand);
      key.push_back(position == positions.end() ? ~uintptr_t(0)
                                                : position->second);
    }
    for (Value result : op->getResults()) {
      key.push_back(
          reinterpret_cast<uintptr_t>(result.getType().getAsOpaquePointer()));
      positions[result] = next++;
    }
  });
  return key;
}

struct FunctionFamily {
  uint64_t count = 0;
  uint64_t ops = 0;
};

struct GlobalFamily {
  uint64_t count = 0;
  uint64_t ops = 0;
  bool addresses = false;
  std::set<std::vector<uintptr_t>> contents;
  std::set<std::vector<uintptr_t>> contentsIgnoringAddresses;
};

uint64_t countNested(Operation *op) {
  uint64_t count = 0;
  op->walk([&](Operation *nested) { count += nested != op; });
  return count;
}

class NativeSizeCensusTestPass
    : public PassWrapper<NativeSizeCensusTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NativeSizeCensusTestPass)

  NativeSizeCensusTestPass() = default;
  NativeSizeCensusTestPass(const NativeSizeCensusTestPass &other)
      : PassWrapper(other) {}

  StringRef getArgument() const final {
    return "test-talos-print-native-size";
  }
  StringRef getDescription() const final {
    return "print operation counts of a lowered native module by function "
           "and global family";
  }

  Option<unsigned> limit{*this, "limit",
                         llvm::cl::desc("print at most this many families of "
                                        "each kind (0: all)"),
                         llvm::cl::init(0)};

  void runOnOperation() final {
    std::map<std::string, FunctionFamily> functions;
    std::map<std::string, GlobalFamily> globals;
    uint64_t total = 0, functionOps = 0, globalOps = 0, functionCount = 0,
             globalCount = 0;
    getOperation()->walk([&](Operation *op) {
      ++total;
      if (auto function = dyn_cast<LLVM::LLVMFuncOp>(op)) {
        if (function.isExternal())
          return;
        FunctionFamily &family = functions[familyOf(function.getSymName())];
        uint64_t ops = countNested(function);
        ++family.count;
        family.ops += ops;
        functionOps += ops;
        ++functionCount;
        return;
      }
      auto global = dyn_cast<LLVM::GlobalOp>(op);
      if (!global)
        return;
      GlobalFamily &family = globals[familyOf(global.getSymName())];
      uint64_t ops = countNested(global);
      ++family.count;
      family.ops += ops;
      globalOps += ops;
      ++globalCount;
      Region &initializer = global.getInitializerRegion();
      if (initializer.empty()) {
        // An attribute initializer (or none) is its own content key.
        Attribute value = global.getValueOrNull();
        if (value)
          family.addresses |=
              value.walk([](SymbolRefAttr) { return WalkResult::interrupt(); })
                  .wasInterrupted();
        std::vector<uintptr_t> key{
            reinterpret_cast<uintptr_t>(value.getAsOpaquePointer())};
        family.contents.insert(key);
        family.contentsIgnoringAddresses.insert(key);
        return;
      }
      family.addresses |=
          initializer
              .walk([](LLVM::AddressOfOp) { return WalkResult::interrupt(); })
              .wasInterrupted();
      family.contents.insert(contentKey(initializer, false));
      family.contentsIgnoringAddresses.insert(contentKey(initializer, true));
    });

    llvm::raw_ostream &os = llvm::errs();
    os << "native-size ops=" << total << " function-ops=" << functionOps
       << " functions=" << functionCount << " global-ops=" << globalOps
       << " globals=" << globalCount << "\n";

    // Largest families first; ties by name so the order is stable.
    auto print = [&](auto &families, auto &&row) {
      using Entry = std::remove_reference_t<decltype(*families.begin())>;
      SmallVector<Entry *> order;
      for (Entry &entry : families)
        order.push_back(&entry);
      llvm::stable_sort(order, [](Entry *lhs, Entry *rhs) {
        if (lhs->second.ops != rhs->second.ops)
          return lhs->second.ops > rhs->second.ops;
        return lhs->first < rhs->first;
      });
      if (limit && order.size() > limit)
        order.resize(limit);
      for (Entry *entry : order)
        row(entry->first, entry->second);
    };
    print(functions, [&](StringRef name, const FunctionFamily &family) {
      os << "function " << name << " count=" << family.count
         << " ops=" << family.ops << "\n";
    });
    print(globals, [&](StringRef name, const GlobalFamily &family) {
      os << "global " << name << " count=" << family.count
         << " ops=" << family.ops << " distinct=" << family.contents.size()
         << " distinct-ignoring-addresses="
         << family.contentsIgnoringAddresses.size()
         << " addresses=" << (family.addresses ? "yes" : "no") << "\n";
    });
  }
};

} // namespace

namespace talos {
void registerNativeSizeCensusTestPass() {
  PassRegistration<NativeSizeCensusTestPass>();
}
} // namespace talos
