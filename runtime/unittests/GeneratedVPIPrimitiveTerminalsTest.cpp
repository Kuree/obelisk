//===- GeneratedVPIPrimitiveTerminalsTest.cpp - Primitive connectivity
//-----===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, PrimitiveTerminals) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  RecordProperty("design_database_bytes", encoded->designDatabase.size());
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(raw, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const char *name) {
    auto handle = keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
    EXPECT_NE(handle, nullptr) << name;
    return handle;
  };
  auto children = [&](vpiHandle source, int selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, source);
    if (!iterator)
      return result;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, source), 1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto contains = [&](const std::vector<vpiHandle> &list, vpiHandle value) {
    return std::any_of(list.begin(), list.end(), [&](vpiHandle h) {
      return vpi_compare_objects(h, value) == 1;
    });
  };
  struct Primitive {
    const char *name;
    int kind;
    std::vector<int> directions;
    std::vector<const char *> connections;
  };
  const Primitive primitives[] = {
      {"top.g",
       vpiGate,
       {vpiOutput, vpiInput, vpiInput},
       {"top.out", "top.a", "top.b"}},
      {"top.g2",
       vpiGate,
       {vpiOutput, vpiInput, vpiInput},
       {"top.out", "top.a", "top.b"}},
      {"top.multi",
       vpiGate,
       {vpiOutput, vpiOutput, vpiInput},
       {"top.multi0", "top.multi1", "top.a"}},
      {"top.sw",
       vpiSwitch,
       {vpiInout, vpiInout, vpiInput},
       {"top.x", "top.y", "top.a"}},
      {"top.u", vpiUdp, {vpiOutput, vpiInput}, {"top.uout", "top.a"}},
      {"other.g", vpiGate, {vpiOutput, vpiInput}, {"other.remote", "top.a"}}};
  vpiHandle remoteInput = nullptr;
  for (const auto &spec : primitives) {
    auto primitive = named(spec.name);
    EXPECT_EQ(vpi_get(vpiType, primitive), spec.kind);
    EXPECT_EQ(
        vpi_get(vpiSize, primitive),
        std::count(spec.directions.begin(), spec.directions.end(), vpiInput));
    auto terminals = children(primitive, vpiPrimTerm);
    ASSERT_EQ(terminals.size(), spec.directions.size()) << spec.name;
    auto repeated = children(primitive, vpiPrimTerm);
    ASSERT_EQ(repeated.size(), terminals.size());
    for (size_t index = 0; index != terminals.size(); ++index) {
      auto terminal = terminals[index];
      EXPECT_EQ(vpi_get(vpiType, terminal), vpiPrimTerm);
      EXPECT_EQ(vpi_get(vpiDirection, terminal), spec.directions[index]);
      EXPECT_EQ(vpi_get(vpiTermIndex, terminal), index);
      EXPECT_EQ(vpi_compare_objects(terminal, repeated[index]), 1);
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiPrimitive, terminal)),
                                    primitive),
                1);
      auto net = named(spec.connections[index]);
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiExpr, terminal)), net),
                1);
      EXPECT_TRUE(contains(children(net, vpiPrimTerm), terminal));
      EXPECT_EQ(contains(children(net, vpiDriver), terminal),
                spec.directions[index] != vpiInput);
      EXPECT_EQ(contains(children(net, vpiLoad), terminal),
                spec.directions[index] != vpiOutput);
      bool local =
          (std::string(spec.name).compare(0, 4, "top.") == 0) ==
          (std::string(spec.connections[index]).compare(0, 4, "top.") == 0);
      EXPECT_EQ(contains(children(net, vpiLocalDriver), terminal),
                local && spec.directions[index] != vpiInput);
      EXPECT_EQ(contains(children(net, vpiLocalLoad), terminal),
                local && spec.directions[index] != vpiOutput);
      if (std::string(spec.name) == "other.g" && index == 1)
        remoteInput = terminal;
    }
  }
  ASSERT_NE(remoteInput, nullptr);
  auto a = named("top.a");
  EXPECT_EQ(children(a, vpiLoad).size(), 6u);
  EXPECT_EQ(children(a, vpiLocalLoad).size(), 5u);
  EXPECT_FALSE(contains(children(a, vpiLocalLoad), remoteInput));

  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  uint64_t terminalCount = 0;
  for (uint64_t i = 0; i != directory.getStaticObjectCount(); ++i) {
    StaticObjectView row(image.data() + directory.getStaticObjectOffset() +
                         i * StaticObjectLayout.size);
    if (row.getVPIKind() != vpiPrimTerm)
      continue;
    ++terminalCount;
    EXPECT_EQ(row.getName(), 0u);
    EXPECT_EQ(row.getFlags(), 0u);
  }
  ASSERT_EQ(terminalCount, 16u);
  // Compare actual encodings with and without terminal inventory. The exact
  // delta uses generated table sizes, not a fragile whole-image size limit.
  auto withoutModule = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
  auto withoutDesign =
      *withoutModule->getOps<obelisk::sim::SimDesignOp>().begin();
  llvm::SmallVector<mlir::Operation *> erase;
  llvm::SmallVector<llvm::StringRef> terminalSymbols;
  for (auto anchor : withoutDesign.getOps<obelisk::sim::SimVPIObjectAnchorOp>())
    if (anchor.getVpiKind() == vpiPrimTerm) {
      terminalSymbols.push_back(anchor.getSymName());
      erase.push_back(anchor);
    }
  for (auto relation :
       withoutDesign.getOps<obelisk::sim::SimVPIRelationDeclOp>()) {
    auto terminalReference = [&](obelisk::sim::VPIObjectRefAttr ref) {
      return ref.getSymbol() &&
             llvm::is_contained(terminalSymbols, ref.getSymbol().getValue());
    };
    if (terminalReference(relation.getSource()) ||
        terminalReference(relation.getTarget()))
      erase.push_back(relation);
  }
  for (auto *operation : erase)
    operation->erase();
  auto without = obelisk::encodeSimulationDesign(withoutDesign, options);
  ASSERT_TRUE(mlir::succeeded(without));
  HeaderView withoutHeader(without->designDatabase.data());
  SemanticDirectoryView withoutDirectory(without->designDatabase.data() +
                                         withoutHeader.getReserved());
  EXPECT_EQ(header.getObjectCount(), withoutHeader.getObjectCount());
  EXPECT_EQ(directory.getStaticObjectCount() -
                withoutDirectory.getStaticObjectCount(),
            terminalCount);
  EXPECT_EQ(directory.getFixedPropertyCount() -
                withoutDirectory.getFixedPropertyCount(),
            terminalCount);
  uint64_t actualGrowth = image.size() - without->designDatabase.size();
  uint64_t compactGrowth =
      terminalCount * (StaticObjectLayout.size + FixedPropertyLayout.size) +
      (header.getRelationCount() - withoutHeader.getRelationCount()) *
          RelationLayout.size;
  EXPECT_EQ(header.getIndexCount(), withoutHeader.getIndexCount());
  EXPECT_EQ(header.getStringSize(), withoutHeader.getStringSize());
  EXPECT_EQ(actualGrowth, compactGrowth);
  uint64_t fullRecordGrowth =
      compactGrowth +
      terminalCount * (ObjectLayout.size - StaticObjectLayout.size +
                       FixedPropertyLayout.size);
  EXPECT_LT(actualGrowth, fullRecordGrowth);
  RecordProperty("saved_terminal_bytes", fullRecordGrowth - actualGrowth);
  auto cursorFor = [&](const char *name) {
    obelisk_rt_design_cursor_v1 cursor{};
    EXPECT_EQ(obelisk_rt_v1_design_lookup(
                  &execution, reinterpret_cast<const uint8_t *>(name),
                  std::char_traits<char>::length(name), &cursor),
              OBELISK_RT_OK);
    return cursor;
  };
  auto primitive = cursorFor("top.g");
  VPIRelationRange range{};
  ASSERT_EQ(obelisk_rt_cached_vpi_relation_range(raw, primitive, vpiPrimTerm,
                                                 true, &range),
            OBELISK_RT_OK);
  ASSERT_EQ(range.count, 3u);
  obelisk_rt_design_cursor_v1 terminal{};
  uint32_t kind = 0;
  bool statement = false;
  ASSERT_EQ(obelisk_rt_cached_vpi_relation_target(raw, range.first, &terminal,
                                                  &kind, &statement),
            OBELISK_RT_OK);
  uint32_t packed = 0, primitivePacked = 0, otherPrimitive = 0;
  ASSERT_TRUE(tryPackTableIndex(TableKind::Object,
                                (primitive.offset - header.getObjectOffset()) /
                                    ObjectLayout.size,
                                primitivePacked));
  ASSERT_GE(terminal.offset, directory.getStaticObjectOffset());
  ASSERT_LT(terminal.offset,
            directory.getStaticObjectOffset() +
                directory.getStaticObjectCount() * StaticObjectLayout.size);
  ASSERT_TRUE(
      tryPackTableIndex(TableKind::StaticObject,
                        (terminal.offset - directory.getStaticObjectOffset()) /
                            StaticObjectLayout.size,
                        packed));
  ASSERT_TRUE(tryPackTableIndex(
      TableKind::Object,
      (cursorFor("top.g2").offset - header.getObjectOffset()) /
          ObjectLayout.size,
      otherPrimitive));
  uint64_t directionRow = 0, forwardRow = 0, reverseRow = 0;
  for (uint64_t i = 0; i != directory.getFixedPropertyCount(); ++i) {
    uint64_t offset =
        directory.getFixedPropertyOffset() + i * FixedPropertyLayout.size;
    FixedPropertyView row(image.data() + offset);
    if (row.getSourceIndexAndTable() != packed)
      continue;
    if (row.getSelector() == vpiDirection)
      directionRow = offset;
    EXPECT_NE(row.getSelector(), vpiTermIndex);
  }
  for (uint64_t i = 0; i != header.getRelationCount(); ++i) {
    uint64_t offset = header.getRelationOffset() + i * RelationLayout.size;
    RelationView row(image.data() + offset);
    uint32_t source = 0;
    ASSERT_TRUE(tryPackTableIndex(
        unpackRelationSourceTable(row.getSourceKindAndTable()),
        row.getSourceIndex(), source));
    if (source == primitivePacked && row.getTargetIndexAndTable() == packed &&
        row.getSelector() == vpiPrimTerm &&
        relationSourceIsIterate(row.getSourceKindAndTable()))
      forwardRow = offset;
    if (source == packed && row.getSelector() == vpiPrimitive &&
        !relationSourceIsIterate(row.getSourceKindAndTable()))
      reverseRow = offset;
  }
  ASSERT_NE(directionRow, 0u);
  ASSERT_NE(forwardRow, 0u);
  ASSERT_NE(reverseRow, 0u);
  size_t mutationIndex = 0;
  auto rejects = [&](auto mutate) {
    SCOPED_TRACE(mutationIndex++);
    auto bytes = image;
    mutate(bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i != bytes.size(); ++i) {
      hash ^= i >= field::HeaderChecksum && i < field::HeaderChecksum + 8
                  ? 0
                  : bytes[i];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
              OBELISK_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) {
    FixedPropertyWriter(bytes.data() + directionRow).setPayload(vpiMixedIO);
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + forwardRow).setOrdinal(1);
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + reverseRow)
        .setTargetIndexAndTable(otherPrimitive);
  });
  rejects([&](auto &bytes) {
    StaticObjectWriter(bytes.data() + terminal.offset).setFlags(1);
  });
  rejects([&](auto &bytes) {
    StaticObjectWriter(bytes.data() + terminal.offset).setScopeIndex(2);
  });
  // Removing the exact Direction fact must not fall back to a guessed value.
  rejects([&](auto &bytes) {
    auto end = directory.getFixedPropertyOffset() +
               directory.getFixedPropertyCount() * FixedPropertyLayout.size;
    std::move(bytes.begin() + directionRow + FixedPropertyLayout.size,
              bytes.begin() + end, bytes.begin() + directionRow);
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setFixedPropertyCount(directory.getFixedPropertyCount() - 1);
  });
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
