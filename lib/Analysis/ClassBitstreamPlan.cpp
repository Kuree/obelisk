//===- ClassBitstreamPlan.cpp - Canonical object cast metadata -----------===//

#include "obelisk/Analysis/ClassBitstreamPlan.h"

#include "obelisk/Analysis/ClassBitstreamAnalysis.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Dialect/Simulation/SimulationMetadata.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/SymbolTable.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/DataLayout.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

using namespace mlir;

namespace obelisk::analysis {
namespace {

constexpr uint32_t kBlobMagic = OBELISK_RT_CLASS_BITSTREAM_BLOB_MAGIC;
constexpr uint32_t kBlobVersion = OBELISK_RT_CLASS_BITSTREAM_BLOB_VERSION;
constexpr uint64_t kHeaderSize = sizeof(obelisk_rt_class_bitstream_header_v1);
constexpr uint64_t kSiteSize = sizeof(obelisk_rt_class_bitstream_site_v1);
constexpr uint64_t kGroupSize = sizeof(obelisk_rt_class_bitstream_group_v1);
constexpr uint64_t kMemberSize = 8;
constexpr uint64_t kSchemaSize = sizeof(obelisk_rt_class_bitstream_schema_v1);
constexpr uint64_t kFieldSize = sizeof(obelisk_rt_class_bitstream_field_v1);
constexpr uint32_t kNoBytecode = OBELISK_RT_CLASS_BITSTREAM_NO_BYTECODE;

void append32(SmallVectorImpl<uint8_t> &bytes, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes.push_back(static_cast<uint8_t>(value >> (index * 8)));
}

void append64(SmallVectorImpl<uint8_t> &bytes, uint64_t value) {
  for (unsigned index = 0; index != 8; ++index)
    bytes.push_back(static_cast<uint8_t>(value >> (index * 8)));
}

void write64(MutableArrayRef<uint8_t> bytes, uint64_t offset, uint64_t value) {
  for (unsigned index = 0; index != 8; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void write32(MutableArrayRef<uint8_t> bytes, uint64_t offset, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

uint64_t read64(ArrayRef<uint8_t> bytes, uint64_t offset) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= uint64_t{bytes[offset + index]} << (index * 8);
  return value;
}

SmallVector<uint8_t> encodePlan(ArrayRef<uint64_t> plan) {
  SmallVector<uint8_t> bytes;
  bytes.reserve(plan.size() * 8);
  for (uint64_t word : plan)
    append64(bytes, word);
  return bytes;
}

bool isObjectPlan(ArrayRef<int64_t> plan) {
  if (plan.size() < OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_HEADER_WORDS)
    return false;
  uint64_t identity = static_cast<uint64_t>(plan[0]);
  return static_cast<uint32_t>(identity) ==
             OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_MAGIC &&
         static_cast<uint32_t>(identity >> 32) ==
             OBELISK_RT_RECURSIVE_BITSTREAM_PLAN_OBJECT_VERSION;
}

struct Site {
  sim::SimRecursiveExportBitstreamOp operation;
  uint64_t id = 0;
  SmallVector<uint8_t> plan;
  uint64_t planOffset = 0;
  bool knownNull = false;
};

struct Group {
  sim::ClassHandleType source;
  bool allowHiddenRoot = false;
  uint64_t id = 0;
  SmallVector<uint64_t> members;
  uint64_t firstMember = 0;
};

struct Field {
  uint64_t offset = 0;
  uint64_t planeSize = 0;
  uint64_t rootSpan = 0;
  uint32_t flags = 0;
  uint32_t alignment = 0;
  SmallVector<uint8_t> plan;
  uint64_t planOffset = 0;
};

struct Schema {
  const ClassBitstreamAnalysis::Schema *source = nullptr;
  uint64_t classID = 0;
  uint64_t instanceSize = 0;
  uint32_t instanceAlignment = 0;
  SmallVector<Field> fields;
  uint64_t firstField = 0;
};

} // namespace

LogicalResult
materializeClassBitstreamPlan(sim::SimDesignOp design,
                              const llvm::DataLayout &dataLayout) {
  ModuleOp module = design->getParentOfType<ModuleOp>();
  if (!module)
    return design.emitOpError("class bit-stream planning requires a module");
  MLIRContext *context = design.getContext();
  OpBuilder builder(context);
  SmallVector<Site> sites;
  design.walk([&](sim::SimRecursiveExportBitstreamOp operation) {
    if (!isObjectPlan(operation.getPlan()))
      return;
    uint64_t id = sites.size() + 1;
    operation.setClassSiteIdAttr(builder.getI64IntegerAttr(id));
    // Bytecode bindings are compiler-produced authority, never input IR.
    // Clear stale or hand-authored values before building the trusted image;
    // the encoder patches exact function/site pairs only after emission.
    operation->removeAttr(sim::metadata::classBitstreamBytecodeFunction);
    operation->removeAttr(sim::metadata::classBitstreamBytecodeSite);
    SmallVector<uint64_t> words;
    words.reserve(operation.getPlan().size());
    for (int64_t word : operation.getPlan())
      words.push_back(static_cast<uint64_t>(word));
    sites.push_back(
        {operation, id, encodePlan(words), 0,
         static_cast<bool>(
             operation.getInput().getDefiningOp<sim::SimClassNullOp>())});
  });
  if (sites.empty()) {
    module->removeAttr(sim::metadata::classBitstreamBlob);
    return success();
  }

  FailureOr<ClassBitstreamAnalysis> analyzed =
      ClassBitstreamAnalysis::compute(design, dataLayout);
  if (failed(analyzed))
    return failure();

  SmallVector<Group> groups;
  DenseMap<uint64_t, unsigned> groupIndices;
  auto enqueueGroup = [&](sim::ClassHandleType source,
                          bool allowHiddenRoot) -> FailureOr<uint64_t> {
    uint64_t id = sim::getClassBitStreamGroupID(source, allowHiddenRoot);
    auto found = groupIndices.find(id);
    if (found != groupIndices.end()) {
      const Group &existing = groups[found->second];
      if (existing.source != source ||
          existing.allowHiddenRoot != allowHiddenRoot)
        return design.emitOpError(
                   "class bit-stream dispatch-group ID collision"),
               failure();
      return id;
    }
    groupIndices[id] = groups.size();
    groups.push_back({source, allowHiddenRoot, id, {}, 0});
    return id;
  };

  // Regenerate every site plan from its type and privilege marker so group
  // discovery never trusts hand-authored record IDs.
  for (Site &site : sites) {
    bool allowHiddenRoot =
        site.operation.getClassAllowHiddenRoot().value_or(false);
    if (allowHiddenRoot &&
        !isa<sim::ClassHandleType>(site.operation.getInput().getType()))
      return site.operation.emitOpError(
          "hidden class bit-stream root requires a direct class handle");
    bool failedGroup = false;
    std::optional<SmallVector<uint64_t>> plan = sim::getRecursiveBitStreamPlan(
        site.operation.getInput().getType(),
        [&](sim::ClassHandleType source,
            bool directRoot) -> std::optional<uint64_t> {
          FailureOr<uint64_t> id =
              enqueueGroup(source, allowHiddenRoot && directRoot);
          if (failed(id)) {
            failedGroup = true;
            return std::nullopt;
          }
          return *id;
        });
    if (failedGroup || !plan)
      return site.operation.emitOpError(
          "cannot materialize its class bit-stream source plan");
    SmallVector<int64_t> signedWords;
    signedWords.reserve(plan->size());
    llvm::transform(*plan, std::back_inserter(signedWords),
                    [](uint64_t word) { return static_cast<int64_t>(word); });
    site.operation.setPlanAttr(builder.getDenseI64ArrayAttr(signedWords));
    site.plan = encodePlan(*plan);
  }

  SmallVector<Schema> schemas;
  DenseMap<uint64_t, unsigned> schemaIndices;
  for (size_t groupIndex = 0; groupIndex != groups.size(); ++groupIndex) {
    Group &group = groups[groupIndex];
    FailureOr<const ClassBitstreamAnalysis::CastClosure *> closure =
        analyzed->getCastClosure(group.source, group.allowHiddenRoot);
    if (failed(closure))
      return failure();
    group.members.reserve((*closure)->roots.size());
    for (const ClassBitstreamAnalysis::Schema *root : (*closure)->roots) {
      sim::SimClassDeclOp declaration = root->layout->declaration;
      group.members.push_back(declaration.getId());
    }
    llvm::sort(group.members);
    if (std::adjacent_find(group.members.begin(), group.members.end()) !=
        group.members.end())
      return design.emitOpError(
          "class bit-stream dispatch group contains duplicate members");

    for (const ClassBitstreamAnalysis::Schema *source : (*closure)->schemas) {
      sim::SimClassDeclOp declaration = source->layout->declaration;
      uint64_t classID = declaration.getId();
      if (schemaIndices.count(classID))
        continue;
      Schema schema;
      schema.source = source;
      schema.classID = classID;
      schema.instanceSize = source->layout->size;
      schema.instanceAlignment = source->layout->alignment;
      for (const ManagedClassLayoutAnalysis::Field *sourceField :
           source->fields) {
        sim::SimClassFieldDeclOp fieldDeclaration = sourceField->declaration;
        Field field;
        field.offset = sourceField->offset;
        field.planeSize = sourceField->storage.size;
        field.flags = sourceField->storage.fourState
                          ? OBELISK_RT_CLASS_BITSTREAM_FIELD_FOUR_STATE
                          : 0u;
        field.alignment = sourceField->storage.alignment;
        bool failedNestedGroup = false;
        std::optional<SmallVector<uint64_t>> fieldPlan =
            sim::getRecursiveBitStreamPlan(
                fieldDeclaration.getType(),
                [&](sim::ClassHandleType nested,
                    bool) -> std::optional<uint64_t> {
                  FailureOr<uint64_t> id = enqueueGroup(nested, false);
                  if (failed(id)) {
                    failedNestedGroup = true;
                    return std::nullopt;
                  }
                  return *id;
                },
                /*requireDynamic=*/false);
        if (failedNestedGroup || !fieldPlan || fieldPlan->size() < 4)
          return fieldDeclaration.emitOpError(
              "cannot materialize its class bit-stream field plan");
        field.rootSpan = (*fieldPlan)[2];
        field.plan = encodePlan(*fieldPlan);
        schema.fields.push_back(std::move(field));
      }
      schemaIndices[classID] = schemas.size();
      schemas.push_back(std::move(schema));
    }
  }

  // A null object contributes zero bits, but prune it only after every group
  // above has passed static visibility/cycle/schema legality. This avoids
  // linking the class service for null-only casts without letting folding
  // erase diagnostics.
  SmallVector<Site> retainedSites;
  retainedSites.reserve(sites.size());
  for (Site &site : sites) {
    if (!site.knownNull) {
      site.id = retainedSites.size() + 1;
      site.operation.setClassSiteIdAttr(builder.getI64IntegerAttr(site.id));
      retainedSites.push_back(std::move(site));
      continue;
    }
    builder.setInsertionPoint(site.operation);
    Value result;
    if (auto integer =
            dyn_cast<IntegerType>(site.operation.getResult().getType())) {
      result = arith::ConstantOp::create(
          builder, site.operation.getLoc(), integer,
          builder.getIntegerAttr(integer, APInt::getZero(integer.getWidth())));
    } else {
      auto logic = cast<sim::LogicType>(site.operation.getResult().getType());
      IntegerType plane = IntegerType::get(context, logic.getWidth());
      result = sim::SimLogicConstantOp::create(
          builder, site.operation.getLoc(), logic,
          builder.getIntegerAttr(plane, 0), builder.getIntegerAttr(plane, 0));
    }
    Value matched = arith::ConstantOp::create(builder, site.operation.getLoc(),
                                              builder.getI1Type(),
                                              builder.getBoolAttr(false));
    Value watch = sim::SimManagedWatchNullOp::create(
        builder, site.operation.getLoc(), sim::ManagedWatchType::get(context));
    site.operation->replaceAllUsesWith(ValueRange{result, matched, watch});
    site.operation.erase();
  }
  sites = std::move(retainedSites);
  if (sites.empty()) {
    module->removeAttr(sim::metadata::classBitstreamBlob);
    return success();
  }

  llvm::sort(groups, [](const Group &lhs, const Group &rhs) {
    return lhs.id < rhs.id;
  });
  llvm::sort(schemas, [](const Schema &lhs, const Schema &rhs) {
    return lhs.classID < rhs.classID;
  });
  uint64_t memberCount = 0, fieldCount = 0;
  for (Group &group : groups) {
    group.firstMember = memberCount;
    if (group.members.size() > UINT64_MAX - memberCount)
      return design.emitOpError("class bit-stream member table overflows");
    memberCount += group.members.size();
  }
  for (Schema &schema : schemas) {
    schema.firstField = fieldCount;
    if (schema.fields.size() > UINT64_MAX - fieldCount)
      return design.emitOpError("class bit-stream field table overflows");
    fieldCount += schema.fields.size();
  }

  auto checkedTableEnd = [&](uint64_t begin, uint64_t count,
                             uint64_t size) -> std::optional<uint64_t> {
    if (count > (UINT64_MAX - begin) / size)
      return std::nullopt;
    return begin + count * size;
  };
  uint64_t siteOffset = kHeaderSize;
  std::optional<uint64_t> groupOffset =
      checkedTableEnd(siteOffset, sites.size(), kSiteSize);
  std::optional<uint64_t> memberOffset =
      groupOffset ? checkedTableEnd(*groupOffset, groups.size(), kGroupSize)
                  : std::nullopt;
  std::optional<uint64_t> schemaOffset =
      memberOffset ? checkedTableEnd(*memberOffset, memberCount, kMemberSize)
                   : std::nullopt;
  std::optional<uint64_t> fieldOffset =
      schemaOffset ? checkedTableEnd(*schemaOffset, schemas.size(), kSchemaSize)
                   : std::nullopt;
  std::optional<uint64_t> planOffset =
      fieldOffset ? checkedTableEnd(*fieldOffset, fieldCount, kFieldSize)
                  : std::nullopt;
  if (!planOffset)
    return design.emitOpError("class bit-stream blob tables overflow");

  SmallVector<uint8_t> planBytes;
  DenseMap<uint64_t, SmallVector<std::pair<std::string, uint64_t>, 1>>
      internedPlans;
  auto internPlan = [&](ArrayRef<uint8_t> bytes) -> FailureOr<uint64_t> {
    std::string key(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    uint64_t hash = static_cast<uint64_t>(llvm::hash_value(key));
    for (const auto &[existing, offset] : internedPlans[hash])
      if (existing == key)
        return *planOffset + offset;
    uint64_t offset = planBytes.size();
    if (bytes.size() > UINT64_MAX - offset)
      return failure();
    llvm::append_range(planBytes, bytes);
    internedPlans[hash].push_back({std::move(key), offset});
    return *planOffset + offset;
  };
  for (Site &site : sites) {
    FailureOr<uint64_t> offset = internPlan(site.plan);
    if (failed(offset))
      return design.emitOpError("class bit-stream site plans overflow");
    site.planOffset = *offset;
  }
  for (Schema &schema : schemas)
    for (Field &field : schema.fields) {
      FailureOr<uint64_t> offset = internPlan(field.plan);
      if (failed(offset))
        return design.emitOpError("class bit-stream field plans overflow");
      field.planOffset = *offset;
    }
  if (planBytes.size() > UINT64_MAX - *planOffset)
    return design.emitOpError("class bit-stream blob size overflows");
  uint64_t blobSize = *planOffset + planBytes.size();
  if (blobSize > std::numeric_limits<size_t>::max())
    return design.emitOpError("class bit-stream blob exceeds host resources");

  SmallVector<uint8_t> blob(static_cast<size_t>(kHeaderSize), 0);
  blob.reserve(static_cast<size_t>(*planOffset));
  auto appendSite = [&](const Site &site) {
    append64(blob, site.id);
    auto function = site.operation->getAttrOfType<IntegerAttr>(
        sim::metadata::classBitstreamBytecodeFunction);
    auto bytecodeSite = site.operation->getAttrOfType<IntegerAttr>(
        sim::metadata::classBitstreamBytecodeSite);
    append32(blob,
             function ? static_cast<uint32_t>(function.getInt()) : kNoBytecode);
    append32(blob, bytecodeSite ? static_cast<uint32_t>(bytecodeSite.getInt())
                                : kNoBytecode);
    append64(blob, site.planOffset);
    append64(blob, site.plan.size());
    append64(blob, 0);
  };
  for (const Site &site : sites)
    appendSite(site);
  for (const Group &group : groups) {
    append64(blob, group.id);
    // Recover the declared static class directly.  An abstract or interface
    // handle may intentionally have no compatible concrete roots in the
    // closed-world design, in which case its dispatch group is empty.
    sim::SimClassDeclOp staticClass;
    // The compatible root list is sorted base-compatible, but its first exact
    // class need not be the static abstract class. Recover the declared ID via
    // the design symbol table.
    staticClass = SymbolTable::lookupNearestSymbolFrom<sim::SimClassDeclOp>(
        design, group.source.getClassName());
    if (!staticClass)
      return design.emitOpError(
          "class bit-stream group references an unknown static class");
    append64(blob, staticClass.getId());
    append64(blob, group.firstMember);
    append64(blob, group.members.size());
    append32(blob, group.allowHiddenRoot ? 1u : 0u);
    append32(blob, 0);
  }
  for (const Group &group : groups)
    for (uint64_t member : group.members)
      append64(blob, member);
  for (const Schema &schema : schemas) {
    append64(blob, schema.classID);
    append64(blob, schema.instanceSize);
    append64(blob, schema.firstField);
    append64(blob, schema.fields.size());
    append32(blob, schema.instanceAlignment);
    append32(blob, 0);
    append64(blob, 0);
  }
  for (const Schema &schema : schemas)
    for (const Field &field : schema.fields) {
      append64(blob, field.offset);
      append64(blob, field.planeSize);
      append64(blob, field.rootSpan);
      append64(blob, field.planOffset);
      append64(blob, field.plan.size());
      append32(blob, field.flags);
      append32(blob, field.alignment);
    }
  if (blob.size() != *planOffset)
    return design.emitOpError("class bit-stream blob table layout drifted");
  llvm::append_range(blob, planBytes);
  if (blob.size() != blobSize)
    return design.emitOpError("class bit-stream blob size drifted");

  write64(blob, 0, uint64_t{kBlobMagic} | (uint64_t{kBlobVersion} << 32));
  write64(blob, 8, blobSize);
  write64(blob, 16, siteOffset);
  write64(blob, 24, sites.size());
  write64(blob, 32, *groupOffset);
  write64(blob, 40, groups.size());
  write64(blob, 48, *memberOffset);
  write64(blob, 56, memberCount);
  write64(blob, 64, *schemaOffset);
  write64(blob, 72, schemas.size());
  write64(blob, 80, *fieldOffset);
  write64(blob, 88, fieldCount);
  write64(blob, 96, *planOffset);
  write64(blob, 104, planBytes.size());
  module->setAttr(
      sim::metadata::classBitstreamBlob,
      builder.getDenseI8ArrayAttr(ArrayRef<int8_t>(
          reinterpret_cast<const int8_t *>(blob.data()), blob.size())));
  return success();
}

LogicalResult bindClassBitstreamBytecodeSites(sim::SimDesignOp design) {
  ModuleOp module = design->getParentOfType<ModuleOp>();
  auto attribute = module ? module->getAttrOfType<DenseI8ArrayAttr>(
                                sim::metadata::classBitstreamBlob)
                          : DenseI8ArrayAttr{};
  if (!module || !attribute ||
      static_cast<uint64_t>(attribute.size()) < kHeaderSize)
    return design.emitOpError(
        "bytecode class-site binding requires a materialized blob");
  ArrayRef<int8_t> signedBytes = attribute.asArrayRef();
  SmallVector<uint8_t> bytes;
  bytes.reserve(signedBytes.size());
  for (int8_t byte : signedBytes)
    bytes.push_back(static_cast<uint8_t>(byte));
  uint64_t identity = read64(bytes, 0);
  uint64_t blobSize = read64(bytes, 8);
  uint64_t siteOffset = read64(bytes, 16);
  uint64_t siteCount = read64(bytes, 24);
  if (static_cast<uint32_t>(identity) != kBlobMagic ||
      static_cast<uint32_t>(identity >> 32) != kBlobVersion ||
      blobSize != bytes.size() || siteOffset > blobSize ||
      siteCount > (blobSize - siteOffset) / kSiteSize)
    return design.emitOpError(
        "materialized class bit-stream blob has an invalid site table");
  SmallVector<uint8_t> seen(siteCount);
  bool invalid = false;
  design.walk([&](sim::SimRecursiveExportBitstreamOp operation) {
    auto site = operation.getClassSiteIdAttr();
    if (!site)
      return;
    uint64_t id = site.getValue().getZExtValue();
    auto function = operation->getAttrOfType<IntegerAttr>(
        sim::metadata::classBitstreamBytecodeFunction);
    auto bytecodeSite = operation->getAttrOfType<IntegerAttr>(
        sim::metadata::classBitstreamBytecodeSite);
    if (id == 0 || id > siteCount || seen[id - 1] || !function ||
        !bytecodeSite || function.getValue().isNegative() ||
        bytecodeSite.getValue().isNegative() ||
        function.getValue().getActiveBits() > 32 ||
        bytecodeSite.getValue().getActiveBits() > 32 ||
        function.getValue().getZExtValue() == kNoBytecode ||
        bytecodeSite.getValue().getZExtValue() == kNoBytecode) {
      operation.emitOpError("has an invalid bytecode class-site binding");
      invalid = true;
      return;
    }
    uint64_t record = siteOffset + (id - 1) * kSiteSize;
    if (read64(bytes, record) != id) {
      operation.emitOpError("does not match its trusted class-site record");
      invalid = true;
      return;
    }
    write32(bytes, record + 8, static_cast<uint32_t>(function.getInt()));
    write32(bytes, record + 12, static_cast<uint32_t>(bytecodeSite.getInt()));
    seen[id - 1] = 1;
  });
  if (invalid || llvm::is_contained(seen, uint8_t{0}))
    return failure();
  OpBuilder builder(module.getContext());
  module->setAttr(
      sim::metadata::classBitstreamBlob,
      builder.getDenseI8ArrayAttr(ArrayRef<int8_t>(
          reinterpret_cast<const int8_t *>(bytes.data()), bytes.size())));
  return success();
}

} // namespace obelisk::analysis
