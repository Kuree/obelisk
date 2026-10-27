//===- GeneratedVPIMailboxLayoutTest.cpp - Exact message storage ----------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace talos::reflection;

TEST(GeneratedDesignDatabase, MailboxElementLayouts) {
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  talos::sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<talos::sim::SimDesignOp>().begin();
  talos::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  talos_rt_execution_descriptor_v1 execution{TALOS_RT_VERSION,
                                               encoded->executionFlags,
                                               0,
                                               encoded->bytecode.data(),
                                               encoded->bytecode.size(),
                                               encoded->designDatabase.data(),
                                               encoded->designDatabase.size(),
                                               encoded->stateBitCount,
                                               checksum,
                                               nullptr,
                                               0,
                                               0,
                                               0,
                                               nullptr,
                                               0,
                                               nullptr,
                                               0};
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);

  std::array<talos_rt_design_cursor_v1, 8> owners{}, elements{}, physical{},
      roots{};
  const std::array<const char *, 8> names{
      "top.array",  "top.queue", "top.assoc", "top.aggregate_keys",
      "top.tagged", "top.union", "top.enum",  "top.small_tagged"};
  for (size_t index = 0; index != names.size(); ++index) {
    talos_rt_design_cursor_v1 storage{}, root{};
    ASSERT_EQ(talos_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(names[index]),
                  std::strlen(names[index]), &storage),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_design_semantic_root(runtime, storage, &root),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_vpi_semantic_class_typespec(runtime, root,
                                                            &owners[index]),
              TALOS_RT_OK);
    talos_rt_design_semantic_type_edge_v1 edge{};
    ASSERT_EQ(
        talos_rt_cached_design_semantic_type_edge(runtime, root, 0, &edge),
        TALOS_RT_OK);
    roots[index] = root;
    elements[index] = edge.child;
    EXPECT_EQ(talos_rt_cached_vpi_mailbox_element_type(
                  runtime, owners[index], edge.child, &physical[index]),
              TALOS_RT_OK);
  }
  // Arrays preserve their source direction and actual element stride; the
  // record contains padding before/after its managed string field.
  talos_rt_design_type_info_v1 array{}, record{};
  ASSERT_EQ(talos_rt_cached_design_type_info(runtime, physical[0], &array),
            TALOS_RT_OK);
  EXPECT_EQ(array.kind, TALOS_RT_DESIGN_TYPE_ARRAY);
  EXPECT_EQ(array.range_left, 2);
  EXPECT_EQ(array.range_right, 1);
  EXPECT_EQ(array.bit_width, 384u);
  ASSERT_EQ(
      talos_rt_cached_design_type_info(runtime, array.element_type, &record),
      TALOS_RT_OK);
  EXPECT_EQ(record.kind, TALOS_RT_DESIGN_TYPE_STRUCT);
  EXPECT_EQ(record.bit_width, 192u);
  talos_rt_design_semantic_type_edge_v1 recordSemantic{};
  ASSERT_EQ(talos_rt_cached_design_semantic_type_edge(runtime, elements[0], 0,
                                                        &recordSemantic),
            TALOS_RT_OK);
  const std::array<uint64_t, 4> offsets{0, 32, 64, 128};
  for (size_t index = 0; index != offsets.size(); ++index) {
    talos_rt_design_cursor_v1 field{};
    talos_rt_design_type_info_v1 info{};
    ASSERT_EQ(talos_rt_cached_design_type_child(runtime, array.element_type,
                                                  index, &field),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_design_type_info(runtime, field, &info),
              TALOS_RT_OK);
    EXPECT_EQ(info.packed_offset, offsets[index]);
  }
  // The same exact record layout is retained behind queue and associative
  // handles, even though their root physical representation is just 64 bits.
  for (size_t index : {1u, 2u, 3u}) {
    talos_rt_design_type_info_v1 handle{};
    ASSERT_EQ(
        talos_rt_cached_design_type_info(runtime, physical[index], &handle),
        TALOS_RT_OK);
    EXPECT_EQ(handle.kind, TALOS_RT_DESIGN_TYPE_SCALAR);
    EXPECT_EQ(handle.bit_width, 64u);
    const uint64_t ordinal = index == 2 ? 1 : 0;
    talos_rt_design_semantic_type_edge_v1 edge{};
    ASSERT_EQ(talos_rt_cached_design_semantic_type_edge(
                  runtime, elements[index], ordinal, &edge),
              TALOS_RT_OK);
    talos_rt_design_cursor_v1 child{};
    ASSERT_EQ(talos_rt_cached_vpi_mailbox_element_type(runtime, owners[index],
                                                         edge.child, &child),
              TALOS_RT_OK);
    EXPECT_EQ(child.offset, array.element_type.offset);
  }
  // A packed associative key is copied as one integral value; its retained
  // source dimensions do not manufacture an executable array layout.
  talos_rt_design_semantic_type_edge_v1 key{};
  ASSERT_EQ(talos_rt_cached_design_semantic_type_edge(runtime, elements[2], 0,
                                                        &key),
            TALOS_RT_OK);
  talos_rt_design_cursor_v1 missing{123};
  EXPECT_EQ(talos_rt_cached_vpi_mailbox_element_type(runtime, owners[2],
                                                       key.child, &missing),
            TALOS_RT_EOF);
  EXPECT_EQ(missing.offset, 0u);
  EXPECT_EQ(talos_rt_cached_vpi_mailbox_element_type(runtime, owners[0], {},
                                                       &missing),
            TALOS_RT_INVALID_HANDLE);
  EXPECT_EQ(talos_rt_cached_vpi_mailbox_element_type(runtime, owners[0],
                                                       elements[0], nullptr),
            TALOS_RT_INVALID_ARGUMENT);
  for (size_t index : {4u, 5u, 7u}) {
    talos_rt_design_type_info_v1 info{};
    ASSERT_EQ(
        talos_rt_cached_design_type_info(runtime, physical[index], &info),
        TALOS_RT_OK);
    EXPECT_EQ(info.kind, TALOS_RT_DESIGN_TYPE_UNION);
    EXPECT_EQ(info.bit_width, index == 4 ? 130u : index == 5 ? 128u : 10u);
    EXPECT_EQ(info.tag_bits, index == 5 ? 0u : 2u);
  }

  // The complete element includes a tag crossing the payload's byte boundary.
  // Exercise the shared compiler descriptor and actual mailbox copy together.
  auto small = mlir::dyn_cast_or_null<talos::sim::SimVPITypespecDeclOp>(
      mlir::SymbolTable::lookupSymbolIn(design, "small_tagged"));
  ASSERT_TRUE(small);
  auto descriptor =
      talos::sim::getElementDescriptor(small.getMessageTypeAttr().getValue());
  ASSERT_TRUE(descriptor);
  EXPECT_EQ(descriptor->valueSize, 2u);
  EXPECT_EQ(descriptor->bitWidth, 16u);
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(runtime, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  talos_rt_object_v1 *mailbox = nullptr;
  ASSERT_EQ(talos_rt_v1_mailbox_create_typed(
                lane, 991, static_cast<uint32_t>(descriptor->kind),
                static_cast<uint32_t>(descriptor->flags), descriptor->valueSize,
                1, descriptor->bitWidth, nullptr, 0, 0, &mailbox),
            TALOS_RT_OK);
  talos_rt_gc_root_v1 root{};
  ASSERT_EQ(talos_rt_v1_gc_root_push(lane, &root, &mailbox), TALOS_RT_OK);
  const std::array<uint8_t, 2> value{0xa5, 0x02};
  uint32_t success = 0;
  ASSERT_EQ(talos_rt_v1_mailbox_try_put(lane, mailbox, value.data(), nullptr,
                                          &success),
            TALOS_RT_OK);
  ASSERT_EQ(success, 1u);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  std::array<uint8_t, 2> copied{};
  ASSERT_EQ(
      talos_rt_v1_mailbox_try_get(mailbox, copied.data(), nullptr, &success),
      TALOS_RT_OK);
  EXPECT_EQ(success, 1u);
  EXPECT_EQ(copied, value);
  ASSERT_EQ(talos_rt_v1_gc_root_pop(lane, &root), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(runtime);

  const HeaderView header(encoded->designDatabase.data());
  const SemanticDirectoryView directory(encoded->designDatabase.data() +
                                        header.getReserved());
  const uint64_t rows = directory.getValueElementBindingOffset();
  const uint64_t count = directory.getValueElementBindingCount();
  ASSERT_GT(count, 2u);
  auto original = [&] {
    return std::vector<uint8_t>(encoded->designDatabase.begin(),
                                encoded->designDatabase.end());
  };
  auto checked = [&](std::vector<uint8_t> image, talos_rt_status expected) {
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
    EXPECT_EQ(talos_rt_v1_design_validate(&execution), expected);
  };
  auto rejected = [&](std::vector<uint8_t> image) {
    checked(std::move(image), TALOS_RT_INVALID_DESIGN);
  };
  // Exact sparse pairs are unique, canonical, reachable and fully typed.
  auto image = original();
  std::memcpy(image.data() + rows + ValueElementBindingLayout.size,
              image.data() + rows, ValueElementBindingLayout.size);
  rejected(std::move(image));
  image = original();
  ValueElementBindingWriter(image.data() + rows).setOwnerIndex(UINT32_MAX);
  rejected(std::move(image));
  image = original();
  ValueElementBindingWriter(image.data() + rows)
      .setSemanticTypeIndex(directory.getSemanticTypeCount());
  rejected(std::move(image));
  image = original();
  ValueElementBindingWriter(image.data() + rows)
      .setPhysicalTypeIndex(header.getTypeCount());
  rejected(std::move(image));
  image = original();
  // Owner tags are a generated closed namespace, not arbitrary table kinds.
  ValueElementBindingWriter(image.data() + rows).setOwnerKind(UINT32_MAX);
  rejected(std::move(image));
  const uint32_t scalarIndex = static_cast<uint32_t>(
      (physical[1].offset - header.getTypeOffset()) / TypeLayout.size);
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
  // Replacing the exact array layout with a scalar must fail. Select its
  // canonical owner/root pair explicitly: storage owner rows precede mailbox
  // typespec rows and may legitimately already use this scalar type.
  uint64_t arrayRootRow = UINT64_MAX;
  for (uint64_t index = 0; index != count; ++index) {
    ValueElementBindingView row(encoded->designDatabase.data() + rows +
                                index * ValueElementBindingLayout.size);
    if (row.getOwnerKind() ==
            static_cast<uint32_t>(ValueLayoutOwnerKind::ReflectedObject) &&
        row.getOwnerIndex() == packedOwner(0) &&
        row.getSemanticTypeIndex() + 1 == elements[0].offset) {
      ASSERT_NE(row.getPhysicalTypeIndex(), scalarIndex);
      arrayRootRow = index;
    }
  }
  ASSERT_NE(arrayRootRow, UINT64_MAX);
  image = original();
  ValueElementBindingWriter(image.data() + rows +
                            arrayRootRow * ValueElementBindingLayout.size)
      .setPhysicalTypeIndex(scalarIndex);
  rejected(std::move(image));

  const uint32_t queueOwner = packedOwner(1);
  uint64_t hiddenChildRow = UINT64_MAX;
  for (uint64_t index = 0; index != count; ++index) {
    ValueElementBindingView row(encoded->designDatabase.data() + rows +
                                index * ValueElementBindingLayout.size);
    if (row.getOwnerIndex() == queueOwner &&
        row.getSemanticTypeIndex() + 1 == recordSemantic.child.offset)
      hiddenChildRow = index;
  }
  ASSERT_NE(hiddenChildRow, UINT64_MAX);
  image = original();
  std::memmove(image.data() + rows +
                   hiddenChildRow * ValueElementBindingLayout.size,
               image.data() + rows +
                   (hiddenChildRow + 1) * ValueElementBindingLayout.size,
               (count - hiddenChildRow - 1) * ValueElementBindingLayout.size);
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setValueElementBindingCount(count - 1);
  rejected(std::move(image));

  // Relocate the complete table and append one sorted, otherwise valid row for
  // the owner's outer mailbox node. It is not reachable from its element DAG.
  image = original();
  std::vector<std::array<uint32_t, 4>> bindings;
  for (uint64_t index = 0; index != count; ++index) {
    ValueElementBindingView row(image.data() + rows +
                                index * ValueElementBindingLayout.size);
    bindings.push_back({row.getOwnerKind(), row.getOwnerIndex(),
                        row.getSemanticTypeIndex(),
                        row.getPhysicalTypeIndex()});
  }
  bindings.push_back(
      {static_cast<uint32_t>(ValueLayoutOwnerKind::ReflectedObject),
       packedOwner(0), static_cast<uint32_t>(roots[0].offset - 1),
       scalarIndex});
  std::sort(bindings.begin(), bindings.end());
  const uint64_t relocated = (image.size() + 7) & ~uint64_t{7};
  image.resize(relocated + bindings.size() * ValueElementBindingLayout.size);
  for (size_t index = 0; index != bindings.size(); ++index) {
    ValueElementBindingWriter writer(image.data() + relocated +
                                     index * ValueElementBindingLayout.size);
    writer.setOwnerKind(bindings[index][0]);
    writer.setOwnerIndex(bindings[index][1]);
    writer.setSemanticTypeIndex(bindings[index][2]);
    writer.setPhysicalTypeIndex(bindings[index][3]);
  }
  HeaderWriter(image.data()).setImageSize(image.size());
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setValueElementBindingOffset(relocated);
  SemanticDirectoryWriter(image.data() + header.getReserved())
      .setValueElementBindingCount(bindings.size());
  rejected(std::move(image));
  // Legacy mailbox-only layout rows still use the same exact graph proof.
  image = original();
  std::vector<std::array<uint32_t, 3>> legacy;
  for (uint64_t index = 0; index != count; ++index) {
    ValueElementBindingView row(image.data() + rows +
                                index * ValueElementBindingLayout.size);
    if (row.getOwnerKind() !=
        static_cast<uint32_t>(ValueLayoutOwnerKind::ReflectedObject))
      continue;
    bool mailbox = false;
    for (size_t owner = 0; owner != owners.size(); ++owner)
      mailbox |= row.getOwnerIndex() == packedOwner(owner);
    SemanticTypeView semantic(image.data() + directory.getSemanticTypeOffset() +
                              uint64_t{row.getSemanticTypeIndex()} *
                                  SemanticTypeLayout.size);
    if (mailbox && (semantic.getKindAndFlags() & UINT32_C(0xff)) !=
                       TALOS_RT_DESIGN_SEMANTIC_ENUM)
      legacy.push_back({row.getOwnerIndex(), row.getSemanticTypeIndex(),
                        row.getPhysicalTypeIndex()});
  }
  const uint64_t legacyOffset = (image.size() + 7) & ~uint64_t{7};
  image.resize(legacyOffset + legacy.size() * MailboxElementBindingLayout.size);
  for (size_t index = 0; index != legacy.size(); ++index) {
    MailboxElementBindingWriter writer(
        image.data() + legacyOffset + index * MailboxElementBindingLayout.size);
    writer.setTypespecIndexAndTable(legacy[index][0]);
    writer.setSemanticTypeIndex(legacy[index][1]);
    writer.setPhysicalTypeIndex(legacy[index][2]);
  }
  HeaderWriter(image.data()).setImageSize(image.size());
  SemanticDirectoryWriter legacyDirectory(image.data() + header.getReserved());
  legacyDirectory.setMailboxElementBindingOffset(legacyOffset);
  legacyDirectory.setMailboxElementBindingCount(legacy.size());
  // The same owner cannot be claimed by both old and common tables.
  rejected(image);
  legacyDirectory.setValueElementBindingOffset(0);
  legacyDirectory.setValueElementBindingCount(0);
  checked(std::move(image), TALOS_RT_OK);
}
} // namespace
