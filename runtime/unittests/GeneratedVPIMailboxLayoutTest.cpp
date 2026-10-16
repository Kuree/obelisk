//===- GeneratedVPIMailboxLayoutTest.cpp - Exact message storage ----------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace obelisk::reflection;

TEST(GeneratedDesignDatabase, MailboxElementLayouts) {
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
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  obelisk_rt_execution_descriptor_v1 execution{OBELISK_RT_VERSION,
                                               encoded->executionFlags,
                                               0,
                                               encoded->bytecode.data(),
                                               encoded->bytecode.size(),
                                               encoded->designDatabase.data(),
                                               encoded->designDatabase.size(),
                                               encoded->stateBitCount,
                                               checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);

  std::array<obelisk_rt_design_cursor_v1, 8> owners{}, elements{}, physical{},
      roots{};
  const std::array<const char *, 8> names{
      "top.array",  "top.queue", "top.assoc", "top.aggregate_keys",
      "top.tagged", "top.union", "top.enum",  "top.small_tagged"};
  for (size_t index = 0; index != names.size(); ++index) {
    obelisk_rt_design_cursor_v1 storage{}, root{};
    ASSERT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(names[index]),
                  std::strlen(names[index]), &storage),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_cached_design_semantic_root(runtime, storage, &root),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_cached_vpi_semantic_class_typespec(runtime, root,
                                                            &owners[index]),
              OBELISK_RT_OK);
    obelisk_rt_design_semantic_type_edge_v1 edge{};
    ASSERT_EQ(
        obelisk_rt_cached_design_semantic_type_edge(runtime, root, 0, &edge),
        OBELISK_RT_OK);
    roots[index] = root;
    elements[index] = edge.child;
    EXPECT_EQ(obelisk_rt_cached_vpi_mailbox_element_type(
                  runtime, owners[index], edge.child, &physical[index]),
              index == 6 ? OBELISK_RT_EOF : OBELISK_RT_OK);
  }
  // Arrays preserve their source direction and actual element stride; the
  // record contains padding before/after its managed string field.
  obelisk_rt_design_type_info_v1 array{}, record{};
  ASSERT_EQ(obelisk_rt_cached_design_type_info(runtime, physical[0], &array),
            OBELISK_RT_OK);
  EXPECT_EQ(array.kind, OBELISK_RT_DESIGN_TYPE_ARRAY);
  EXPECT_EQ(array.range_left, 2);
  EXPECT_EQ(array.range_right, 1);
  EXPECT_EQ(array.bit_width, 384u);
  ASSERT_EQ(
      obelisk_rt_cached_design_type_info(runtime, array.element_type, &record),
      OBELISK_RT_OK);
  EXPECT_EQ(record.kind, OBELISK_RT_DESIGN_TYPE_STRUCT);
  EXPECT_EQ(record.bit_width, 192u);
  obelisk_rt_design_semantic_type_edge_v1 recordSemantic{};
  ASSERT_EQ(obelisk_rt_cached_design_semantic_type_edge(runtime, elements[0], 0,
                                                        &recordSemantic),
            OBELISK_RT_OK);
  const std::array<uint64_t, 4> offsets{0, 32, 64, 128};
  for (size_t index = 0; index != offsets.size(); ++index) {
    obelisk_rt_design_cursor_v1 field{};
    obelisk_rt_design_type_info_v1 info{};
    ASSERT_EQ(obelisk_rt_cached_design_type_child(runtime, array.element_type,
                                                  index, &field),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_cached_design_type_info(runtime, field, &info),
              OBELISK_RT_OK);
    EXPECT_EQ(info.packed_offset, offsets[index]);
  }
  // The same exact record layout is retained behind queue and associative
  // handles, even though their root physical representation is just 64 bits.
  for (size_t index : {1u, 2u, 3u}) {
    obelisk_rt_design_type_info_v1 handle{};
    ASSERT_EQ(
        obelisk_rt_cached_design_type_info(runtime, physical[index], &handle),
        OBELISK_RT_OK);
    EXPECT_EQ(handle.kind, OBELISK_RT_DESIGN_TYPE_SCALAR);
    EXPECT_EQ(handle.bit_width, 64u);
    const uint64_t ordinal = index == 2 ? 1 : 0;
    obelisk_rt_design_semantic_type_edge_v1 edge{};
    ASSERT_EQ(obelisk_rt_cached_design_semantic_type_edge(
                  runtime, elements[index], ordinal, &edge),
              OBELISK_RT_OK);
    obelisk_rt_design_cursor_v1 child{};
    ASSERT_EQ(obelisk_rt_cached_vpi_mailbox_element_type(runtime, owners[index],
                                                         edge.child, &child),
              OBELISK_RT_OK);
    EXPECT_EQ(child.offset, array.element_type.offset);
  }
  // A packed associative key is copied as one integral value; its retained
  // source dimensions do not manufacture an executable array layout.
  obelisk_rt_design_semantic_type_edge_v1 key{};
  ASSERT_EQ(obelisk_rt_cached_design_semantic_type_edge(runtime, elements[2], 0,
                                                        &key),
            OBELISK_RT_OK);
  obelisk_rt_design_cursor_v1 missing{123};
  EXPECT_EQ(obelisk_rt_cached_vpi_mailbox_element_type(runtime, owners[2],
                                                       key.child, &missing),
            OBELISK_RT_EOF);
  EXPECT_EQ(missing.offset, 0u);
  EXPECT_EQ(obelisk_rt_cached_vpi_mailbox_element_type(runtime, owners[0], {},
                                                       &missing),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_cached_vpi_mailbox_element_type(runtime, owners[0],
                                                       elements[0], nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  for (size_t index : {4u, 5u, 7u}) {
    obelisk_rt_design_type_info_v1 info{};
    ASSERT_EQ(
        obelisk_rt_cached_design_type_info(runtime, physical[index], &info),
        OBELISK_RT_OK);
    EXPECT_EQ(info.kind, OBELISK_RT_DESIGN_TYPE_UNION);
    EXPECT_EQ(info.bit_width, index == 4 ? 130u : index == 5 ? 128u : 10u);
    EXPECT_EQ(info.tag_bits, index == 5 ? 0u : 2u);
  }

  // The complete element includes a tag crossing the payload's byte boundary.
  // Exercise the shared compiler descriptor and actual mailbox copy together.
  auto small = mlir::dyn_cast_or_null<obelisk::sim::SimVPITypespecDeclOp>(
      mlir::SymbolTable::lookupSymbolIn(design, "small_tagged"));
  ASSERT_TRUE(small);
  auto descriptor =
      obelisk::sim::getElementDescriptor(small.getMessageTypeAttr().getValue());
  ASSERT_TRUE(descriptor);
  EXPECT_EQ(descriptor->valueSize, 2u);
  EXPECT_EQ(descriptor->bitWidth, 16u);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(runtime, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_object_v1 *mailbox = nullptr;
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                lane, 991, static_cast<uint32_t>(descriptor->kind),
                static_cast<uint32_t>(descriptor->flags), descriptor->valueSize,
                1, descriptor->bitWidth, nullptr, 0, 0, &mailbox),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &mailbox), OBELISK_RT_OK);
  const std::array<uint8_t, 2> value{0xa5, 0x02};
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_try_put(lane, mailbox, value.data(), nullptr,
                                          &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  std::array<uint8_t, 2> copied{};
  ASSERT_EQ(
      obelisk_rt_v1_mailbox_try_get(mailbox, copied.data(), nullptr, &success),
      OBELISK_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(copied, value);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);

  const HeaderView header(encoded->designDatabase.data());
  const SemanticDirectoryView directory(encoded->designDatabase.data() +
                                        header.getReserved());
  const uint64_t rows = directory.getMailboxElementBindingOffset();
  const uint64_t count = directory.getMailboxElementBindingCount();
  ASSERT_GT(count, 2u);
  auto original = [&] {
    return std::vector<uint8_t>(encoded->designDatabase.begin(),
                                encoded->designDatabase.end());
  };
  auto rejected = [&](std::vector<uint8_t> image) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != image.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : image[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(image.data()).setChecksum(hash);
    execution.design_database = image.data();
    execution.design_database_size = image.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&execution),
              OBELISK_RT_INVALID_DESIGN);
  };
  // Exact sparse pairs are unique, canonical, reachable and fully typed.
  auto image = original();
  std::memcpy(image.data() + rows + MailboxElementBindingLayout.size,
              image.data() + rows, MailboxElementBindingLayout.size);
  rejected(std::move(image));
  image = original();
  MailboxElementBindingWriter(image.data() + rows)
      .setTypespecIndexAndTable(UINT32_MAX);
  rejected(std::move(image));
  image = original();
  MailboxElementBindingWriter(image.data() + rows)
      .setSemanticTypeIndex(directory.getSemanticTypeCount());
  rejected(std::move(image));
  image = original();
  MailboxElementBindingWriter(image.data() + rows)
      .setPhysicalTypeIndex(header.getTypeCount());
  rejected(std::move(image));
  image = original();
  // The last owner has two rows. Removing only its last row leaves a partial
  // layout graph, even though that physical leaf also belongs to other roots.
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setMailboxElementBindingCount(count - 1);
  rejected(std::move(image));
  image = original();
  const uint32_t scalarIndex = static_cast<uint32_t>(
      (physical[1].offset - header.getTypeOffset()) / TypeLayout.size);
  MailboxElementBindingWriter(image.data() + rows)
      .setPhysicalTypeIndex(scalarIndex);
  rejected(std::move(image));
  auto packedOwner = [&](size_t index) {
    uint32_t packed = UINT32_MAX;
    EXPECT_GE(owners[index].offset, header.getObjectOffset());
    const uint64_t relative = owners[index].offset - header.getObjectOffset();
    EXPECT_EQ(relative % ObjectLayout.size, 0u);
    EXPECT_LT(relative / ObjectLayout.size, header.getObjectCount());
    EXPECT_TRUE(
        tryPackTableIndex(TableKind::Object,
                          static_cast<uint32_t>((owners[index].offset -
                                                 header.getObjectOffset()) /
                                                ObjectLayout.size),
                          packed));
    return packed;
  };
  const uint32_t queueOwner = packedOwner(1);
  uint64_t hiddenChildRow = UINT64_MAX;
  for (uint64_t index = 0; index != count; ++index) {
    MailboxElementBindingView row(encoded->designDatabase.data() + rows +
                                  index * MailboxElementBindingLayout.size);
    if (row.getTypespecIndexAndTable() == queueOwner &&
        row.getSemanticTypeIndex() + 1 == recordSemantic.child.offset)
      hiddenChildRow = index;
  }
  ASSERT_NE(hiddenChildRow, UINT64_MAX);
  image = original();
  std::memmove(image.data() + rows +
                   hiddenChildRow * MailboxElementBindingLayout.size,
               image.data() + rows +
                   (hiddenChildRow + 1) * MailboxElementBindingLayout.size,
               (count - hiddenChildRow - 1) * MailboxElementBindingLayout.size);
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setMailboxElementBindingCount(count - 1);
  rejected(std::move(image));

  // Relocate the complete table and append one sorted, otherwise valid row for
  // the owner's outer mailbox node. It is not reachable from its element DAG.
  image = original();
  std::vector<std::array<uint32_t, 3>> bindings;
  for (uint64_t index = 0; index != count; ++index) {
    MailboxElementBindingView row(image.data() + rows +
                                  index * MailboxElementBindingLayout.size);
    bindings.push_back({row.getTypespecIndexAndTable(),
                        row.getSemanticTypeIndex(),
                        row.getPhysicalTypeIndex()});
  }
  bindings.push_back({packedOwner(0),
                      static_cast<uint32_t>(roots[0].offset - 1), scalarIndex});
  std::sort(bindings.begin(), bindings.end());
  const uint64_t relocated = (image.size() + 7) & ~uint64_t{7};
  image.resize(relocated + bindings.size() * MailboxElementBindingLayout.size);
  for (size_t index = 0; index != bindings.size(); ++index) {
    MailboxElementBindingWriter writer(
        image.data() + relocated + index * MailboxElementBindingLayout.size);
    writer.setTypespecIndexAndTable(bindings[index][0]);
    writer.setSemanticTypeIndex(bindings[index][1]);
    writer.setPhysicalTypeIndex(bindings[index][2]);
  }
  HeaderWriter(image.data()).setImageSize(image.size());
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setMailboxElementBindingOffset(relocated);
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setMailboxElementBindingCount(bindings.size());
  rejected(std::move(image));
}
} // namespace
