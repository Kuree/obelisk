//===- SimulationNativePartitionPlanning.cpp - Stable native partitions --===//

#include "obelisk/Conversion/Passes.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <functional>
#include <numeric>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMPLANNATIVEPARTITIONSPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

struct DisjointSets {
  explicit DisjointSets(size_t size) : parent(size), rank(size) {
    std::iota(parent.begin(), parent.end(), 0);
  }

  unsigned find(unsigned value) {
    if (parent[value] != value)
      parent[value] = find(parent[value]);
    return parent[value];
  }

  void unite(unsigned lhs, unsigned rhs) {
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs == rhs)
      return;
    if (rank[lhs] < rank[rhs])
      std::swap(lhs, rhs);
    parent[rhs] = lhs;
    if (rank[lhs] == rank[rhs])
      ++rank[lhs];
  }

  SmallVector<unsigned> parent;
  SmallVector<uint8_t> rank;
};

struct FunctionRecord {
  sim::SimFuncOp function;
  SmallVector<std::string> owners;
  SmallVector<unsigned> callees;
  unsigned partition = 0;
};

template <typename Range> static void sortUnique(Range &values) {
  llvm::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

static std::string unitOwner(sim::SimFuncOp function) {
  if (function.getEntryKind() == sim::EntryKind::RootInitializer)
    return "primary";
  if (auto id = function.getCodeUnitId())
    return (Twine("unit:") + Twine(*id)).str();
  return (Twine("symbol:") + function.getSymName()).str();
}

static std::string makePartitionID(ArrayRef<std::string> owners) {
  if (llvm::is_contained(owners, "primary"))
    return "primary";
  if (owners.size() == 1)
    return owners.front();
  std::string result = "scc:";
  llvm::raw_string_ostream stream(result);
  for (StringRef owner : owners)
    stream << owner.size() << ':' << owner;
  return result;
}

static LogicalResult planNativePartitions(sim::SimDesignOp design) {
  MLIRContext *context = design.getContext();
  Builder builder(context);

  SmallVector<sim::SimFuncOp> functions;
  design.walk([&](sim::SimFuncOp function) {
    function->removeAttr(sim::metadata::nativePartition);
    if (!function.isExternal())
      functions.push_back(function);
  });
  llvm::sort(functions, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });
  design->removeAttr(sim::metadata::nativePartitionManifest);
  if (functions.empty())
    return success();

  llvm::StringMap<unsigned> functionIndices;
  for (auto [index, function] : llvm::enumerate(functions))
    functionIndices.try_emplace(function.getSymName(), index);

  llvm::StringMap<SmallVector<std::string>> classOwners;
  for (sim::SimClassMethodDeclOp method :
       design.getOps<sim::SimClassMethodDeclOp>()) {
    auto implementation = method.getImplementation();
    if (!implementation)
      continue;
    SmallVector<std::string> &owners = classOwners[*implementation];
    owners.push_back((Twine("class:") + method.getOwner()).str());
  }
  for (auto &entry : classOwners) {
    sortUnique(entry.second);
  }

  SmallVector<FunctionRecord> records;
  records.reserve(functions.size());
  for (sim::SimFuncOp function : functions) {
    FunctionRecord &record = records.emplace_back();
    record.function = function;
    auto found = classOwners.find(function.getSymName());
    if (found != classOwners.end())
      record.owners.append(found->second);
    else
      record.owners.push_back(unitOwner(function));
  }

  auto addEdge = [&](unsigned caller, StringRef callee) {
    auto found = functionIndices.find(callee);
    if (found != functionIndices.end())
      records[caller].callees.push_back(found->second);
  };
  for (auto [index, record] : llvm::enumerate(records)) {
    record.function.walk([&](Operation *operation) {
      if (auto call = dyn_cast<sim::SimCallOp>(operation))
        addEdge(index, call.getCallee());
      else if (auto call = dyn_cast<sim::SimClassDirectCallOp>(operation))
        addEdge(index, call.getCallee());
      else if (auto call = dyn_cast<sim::SimTaskCallOp>(operation))
        addEdge(index, call.getCallee());
      else if (auto spawn = dyn_cast<sim::SimSpawnOp>(operation))
        addEdge(index, spawn.getCallee());
    });
    sortUnique(record.callees);
  }

  SmallVector<std::string> ownerKeys;
  for (const FunctionRecord &record : records)
    llvm::append_range(ownerKeys, record.owners);
  sortUnique(ownerKeys);
  llvm::StringMap<unsigned> ownerIndices;
  for (auto [index, owner] : llvm::enumerate(ownerKeys))
    ownerIndices.try_emplace(owner, index);
  DisjointSets owners(ownerKeys.size());
  for (const FunctionRecord &record : records)
    for (StringRef owner : llvm::drop_begin(record.owners))
      owners.unite(ownerIndices[record.owners.front()], ownerIndices[owner]);

  // Tarjan SCCs are used only to merge semantic owners. Their visitation order
  // cannot affect the final manifest because every owner and member inventory
  // is sorted before IDs are formed.
  SmallVector<int> indices(records.size(), -1);
  SmallVector<int> lowLinks(records.size(), -1);
  SmallVector<unsigned> stack;
  SmallVector<bool> onStack(records.size(), false);
  int nextIndex = 0;
  std::function<void(unsigned)> visit = [&](unsigned node) {
    indices[node] = lowLinks[node] = nextIndex++;
    stack.push_back(node);
    onStack[node] = true;
    for (unsigned callee : records[node].callees) {
      if (indices[callee] == -1) {
        visit(callee);
        lowLinks[node] = std::min(lowLinks[node], lowLinks[callee]);
      } else if (onStack[callee]) {
        lowLinks[node] = std::min(lowLinks[node], indices[callee]);
      }
    }
    if (lowLinks[node] != indices[node])
      return;
    SmallVector<unsigned> component;
    while (true) {
      unsigned member = stack.pop_back_val();
      onStack[member] = false;
      component.push_back(member);
      if (member == node)
        break;
    }
    if (component.size() == 1 &&
        !llvm::is_contained(records[node].callees, node))
      return;
    unsigned firstOwner =
        ownerIndices[records[component.front()].owners.front()];
    for (unsigned member : component)
      for (StringRef owner : records[member].owners)
        owners.unite(firstOwner, ownerIndices[owner]);
  };
  for (unsigned index = 0; index != records.size(); ++index)
    if (indices[index] == -1)
      visit(index);

  llvm::DenseMap<unsigned, SmallVector<std::string>> keysByRoot;
  for (auto [index, key] : llvm::enumerate(ownerKeys))
    keysByRoot[owners.find(index)].push_back(key);
  struct Partition {
    std::string id;
    SmallVector<std::string> owners;
    SmallVector<unsigned> members;
    llvm::StringSet<> imports;
    llvm::StringSet<> exports;
    llvm::StringSet<> dependencies;
  };
  SmallVector<Partition> partitions;
  llvm::DenseMap<unsigned, unsigned> partitionByOwnerRoot;
  for (auto &entry : keysByRoot) {
    llvm::sort(entry.second);
    Partition &partition = partitions.emplace_back();
    partition.owners = std::move(entry.second);
    partition.id = makePartitionID(partition.owners);
    partitionByOwnerRoot[entry.first] = partitions.size() - 1;
  }
  llvm::sort(partitions, [](const Partition &lhs, const Partition &rhs) {
    if (lhs.id == "primary")
      return rhs.id != "primary";
    if (rhs.id == "primary")
      return false;
    return lhs.id < rhs.id;
  });
  partitionByOwnerRoot.clear();
  llvm::StringMap<unsigned> partitionByID;
  for (auto [index, partition] : llvm::enumerate(partitions)) {
    partitionByID[partition.id] = index;
    for (StringRef key : partition.owners)
      partitionByOwnerRoot[owners.find(ownerIndices[key])] = index;
  }
  for (auto [index, record] : llvm::enumerate(records)) {
    record.partition =
        partitionByOwnerRoot[owners.find(ownerIndices[record.owners.front()])];
    partitions[record.partition].members.push_back(index);
    record.function->setAttr(
        sim::metadata::nativePartition,
        builder.getStringAttr(partitions[record.partition].id));
  }

  auto addImport = [&](unsigned source, unsigned targetRecord) {
    unsigned target = records[targetRecord].partition;
    if (source == target)
      return;
    StringRef symbol = records[targetRecord].function.getSymName();
    partitions[source].imports.insert(symbol);
    partitions[source].dependencies.insert(partitions[target].id);
    partitions[target].exports.insert(symbol);
  };
  for (auto [index, record] : llvm::enumerate(records))
    for (unsigned callee : record.callees)
      addImport(record.partition, callee);

  // The primary partition owns process/class descriptor tables. Record their
  // address-taken function definitions explicitly even when there is no
  // executable direct-call edge in simulation IR.
  auto primary = partitionByID.find("primary");
  if (primary != partitionByID.end()) {
    for (auto [index, record] : llvm::enumerate(records))
      if (record.partition != primary->second)
        addImport(primary->second, index);
  }

  SmallVector<Attribute> manifest;
  manifest.reserve(partitions.size());
  for (Partition &partition : partitions) {
    llvm::sort(partition.members, [&](unsigned lhs, unsigned rhs) {
      return records[lhs].function.getSymName() <
             records[rhs].function.getSymName();
    });
    SmallVector<Attribute> members;
    for (unsigned member : partition.members)
      members.push_back(FlatSymbolRefAttr::get(
          context, records[member].function.getSymName()));
    auto sortedStrings = [&](const llvm::StringSet<> &values,
                             bool symbols) -> ArrayAttr {
      SmallVector<StringRef> ordered;
      ordered.reserve(values.size());
      for (const auto &entry : values)
        ordered.push_back(entry.getKey());
      llvm::sort(ordered);
      SmallVector<Attribute> attributes;
      attributes.reserve(ordered.size());
      for (StringRef value : ordered)
        attributes.push_back(
            symbols ? Attribute(FlatSymbolRefAttr::get(context, value))
                    : Attribute(builder.getStringAttr(value)));
      return builder.getArrayAttr(attributes);
    };
    SmallVector<Attribute> ownerAttrs;
    for (StringRef owner : partition.owners)
      ownerAttrs.push_back(builder.getStringAttr(owner));
    manifest.push_back(builder.getDictionaryAttr({
        builder.getNamedAttr("id", builder.getStringAttr(partition.id)),
        builder.getNamedAttr("owners", builder.getArrayAttr(ownerAttrs)),
        builder.getNamedAttr("members", builder.getArrayAttr(members)),
        builder.getNamedAttr("imports", sortedStrings(partition.imports, true)),
        builder.getNamedAttr("exports", sortedStrings(partition.exports, true)),
        builder.getNamedAttr("dependencies",
                             sortedStrings(partition.dependencies, false)),
    }));
  }
  design->setAttr(sim::metadata::nativePartitionManifest,
                  builder.getArrayAttr(manifest));
  return success();
}

class ObeliskSimPlanNativePartitionsPass
    : public impl::ObeliskSimPlanNativePartitionsPassBase<
          ObeliskSimPlanNativePartitionsPass> {
public:
  void runOnOperation() override {
    if (failed(planNativePartitions(getOperation())))
      signalPassFailure();
  }
};

} // namespace

namespace detail {

LogicalResult finalizeNativePartitionManifest(ModuleOp module) {
  // The physical inventory is meaningful only when the target requested the
  // semantic plan. Avoid annotating the ordinary unsplit/wasm pipeline.
  if (!module->hasAttr(sim::metadata::nativePartitionManifests)) {
    module->removeAttr(sim::metadata::nativePhysicalPartitionManifest);
    return success();
  }

  // LLVM module assembly and definition-like symbols other than ordinary
  // functions/globals need target-specific ownership rules. Preserve the
  // correctness fallback by declining to publish a physical split plan.
  if (module->hasAttr(LLVM::LLVMDialect::getModuleLevelAsmAttrName())) {
    module->removeAttr(sim::metadata::nativePhysicalPartitionManifest);
    return success();
  }
  for (Operation &operation : module.getBody()->getOperations())
    if (!isa<LLVM::LLVMFuncOp, LLVM::GlobalOp>(operation)) {
      module->removeAttr(sim::metadata::nativePhysicalPartitionManifest);
      return success();
    }
  bool hasBlockAddress = false;
  module.walk([&](LLVM::BlockAddressOp) { hasBlockAddress = true; });
  if (hasBlockAddress) {
    module->removeAttr(sim::metadata::nativePhysicalPartitionManifest);
    return success();
  }

  llvm::StringSet<> validPartitions;
  validPartitions.insert("primary");
  auto semanticManifests =
      module->getAttrOfType<ArrayAttr>(sim::metadata::nativePartitionManifests);
  // Physical symbols share one module namespace. Until partition IDs are
  // qualified by design, duplicate local class/code-unit IDs across designs
  // must retain the unsplit correctness path.
  if (semanticManifests.size() != 1) {
    module->removeAttr(sim::metadata::nativePhysicalPartitionManifest);
    return success();
  }
  for (Attribute designAttr : semanticManifests) {
    auto design = dyn_cast<DictionaryAttr>(designAttr);
    auto partitions =
        design ? design.getAs<ArrayAttr>("partitions") : ArrayAttr{};
    if (!partitions)
      return module.emitError("has a malformed semantic partition manifest");
    for (Attribute partitionAttr : partitions) {
      auto partition = dyn_cast<DictionaryAttr>(partitionAttr);
      auto id = partition ? partition.getAs<StringAttr>("id") : StringAttr{};
      if (!id)
        return module.emitError("has a malformed semantic partition ID");
      validPartitions.insert(id.getValue());
    }
  }

  struct SymbolRecord {
    Operation *operation;
    std::string name;
    std::string partition;
  };
  SmallVector<SymbolRecord> symbols;
  llvm::StringMap<unsigned> symbolIndices;
  Builder builder(module.getContext());
  for (Operation &operation : module.getBody()->getOperations()) {
    bool definition = false;
    if (auto function = dyn_cast<LLVM::LLVMFuncOp>(operation))
      definition = !function.isExternal();
    else if (auto global = dyn_cast<LLVM::GlobalOp>(operation))
      definition = global.getValueAttr() || global.getInitializerBlock();
    else
      continue;
    if (!definition)
      continue;
    std::string name = SymbolTable::getSymbolName(&operation).getValue().str();
    auto partition =
        operation.getAttrOfType<StringAttr>(sim::metadata::nativePartition);
    if (!partition) {
      partition = builder.getStringAttr("primary");
      operation.setAttr(sim::metadata::nativePartition, partition);
    }
    if (!validPartitions.contains(partition.getValue()))
      return operation.emitError("references unknown native partition '")
             << partition.getValue() << "'";
    if (!symbolIndices.try_emplace(name, symbols.size()).second)
      return operation.emitError("duplicates a physical partition symbol");
    symbols.push_back(
        {&operation, std::move(name), partition.getValue().str()});
  }

  struct PhysicalPartition {
    std::string id;
    SmallVector<unsigned> members;
    llvm::StringSet<> imports;
    llvm::StringSet<> exports;
    llvm::StringSet<> dependencies;
  };
  llvm::StringMap<unsigned> partitionIndices;
  SmallVector<PhysicalPartition> partitions;
  for (auto [index, symbol] : llvm::enumerate(symbols)) {
    auto [entry, inserted] =
        partitionIndices.try_emplace(symbol.partition, partitions.size());
    if (inserted) {
      PhysicalPartition &partition = partitions.emplace_back();
      partition.id = symbol.partition;
    }
    partitions[entry->second].members.push_back(index);
  }
  llvm::sort(partitions,
             [](const PhysicalPartition &lhs, const PhysicalPartition &rhs) {
               if (lhs.id == "primary")
                 return rhs.id != "primary";
               if (rhs.id == "primary")
                 return false;
               return lhs.id < rhs.id;
             });
  partitionIndices.clear();
  for (auto [index, partition] : llvm::enumerate(partitions))
    partitionIndices[partition.id] = index;

  for (const SymbolRecord &source : symbols) {
    unsigned sourcePartition = partitionIndices.lookup(source.partition);
    std::optional<SymbolTable::UseRange> uses =
        SymbolTable::getSymbolUses(source.operation);
    if (!uses)
      return source.operation->emitError(
          "cannot enumerate physical partition symbol uses");
    for (const SymbolTable::SymbolUse &use : *uses) {
      StringRef targetName = use.getSymbolRef().getRootReference().getValue();
      auto targetIt = symbolIndices.find(targetName);
      if (targetIt == symbolIndices.end())
        continue;
      const SymbolRecord &target = symbols[targetIt->second];
      unsigned targetPartition = partitionIndices.lookup(target.partition);
      if (sourcePartition == targetPartition)
        continue;
      partitions[sourcePartition].imports.insert(target.name);
      partitions[sourcePartition].dependencies.insert(target.partition);
      partitions[targetPartition].exports.insert(target.name);
    }
  }

  auto sortedStrings = [&](const llvm::StringSet<> &values,
                           bool symbolReferences) -> ArrayAttr {
    SmallVector<StringRef> ordered;
    for (const auto &entry : values)
      ordered.push_back(entry.getKey());
    llvm::sort(ordered);
    SmallVector<Attribute> result;
    result.reserve(ordered.size());
    for (StringRef value : ordered)
      result.push_back(
          symbolReferences
              ? Attribute(FlatSymbolRefAttr::get(module.getContext(), value))
              : Attribute(builder.getStringAttr(value)));
    return builder.getArrayAttr(result);
  };
  SmallVector<Attribute> manifest;
  manifest.reserve(partitions.size());
  for (PhysicalPartition &partition : partitions) {
    llvm::sort(partition.members, [&](unsigned lhs, unsigned rhs) {
      return symbols[lhs].name < symbols[rhs].name;
    });
    SmallVector<Attribute> members;
    for (unsigned member : partition.members)
      members.push_back(
          FlatSymbolRefAttr::get(module.getContext(), symbols[member].name));
    manifest.push_back(builder.getDictionaryAttr({
        builder.getNamedAttr("id", builder.getStringAttr(partition.id)),
        builder.getNamedAttr("members", builder.getArrayAttr(members)),
        builder.getNamedAttr("imports", sortedStrings(partition.imports, true)),
        builder.getNamedAttr("exports", sortedStrings(partition.exports, true)),
        builder.getNamedAttr("dependencies",
                             sortedStrings(partition.dependencies, false)),
    }));
  }
  module->setAttr(sim::metadata::nativePhysicalPartitionManifest,
                  builder.getArrayAttr(manifest));
  return success();
}

} // namespace detail
} // namespace obelisk
