//===- GeneratedVPILiveUnionsTest.cpp - Live union members ----------------===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/StableHandle.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, LiveUnionMembers) {
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
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
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
  uint64_t root = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(
                raw, 44, OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &root),
            OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto children = [&](vpiHandle parent, int selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, parent);
    if (!iterator)
      return result;
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiUse, iterator)), parent),
              1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto read = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  auto unreadable = [&](vpiHandle handle) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    value.value.integer = 923;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(value.value.integer, 923);
  };
  auto metadata = [&](const char *name) {
    obelisk_rt_design_cursor_v1 cursor{};
    EXPECT_EQ(obelisk_rt_cached_design_lookup(
                  raw, reinterpret_cast<const uint8_t *>(name),
                  std::strlen(name), &cursor),
              OBELISK_RT_OK);
    obelisk_rt_design_info_v1 info{};
    EXPECT_EQ(obelisk_rt_cached_design_info(raw, cursor, &info), OBELISK_RT_OK);
    return std::make_pair(cursor, info);
  };
  auto setBits = [&](std::vector<uint8_t> &bytes, uint64_t offset,
                     uint64_t width, uint64_t value) {
    ASSERT_LE(width, 64u);
    ASSERT_LE(offset + width, bytes.size() * 8);
    for (uint64_t bit = 0; bit != width; ++bit) {
      auto mask = uint8_t{1} << ((offset + bit) % 8);
      bytes[(offset + bit) / 8] = (bytes[(offset + bit) / 8] & ~mask) |
                                  (((value >> bit) & 1) ? mask : 0);
    }
  };
  auto field = [&](const obelisk_rt_design_info_v1 &info, unsigned ordinal) {
    obelisk_rt_design_cursor_v1 cursor{};
    EXPECT_EQ(obelisk_rt_cached_design_type_child(raw, {info.type_offset},
                                                  ordinal, &cursor),
              OBELISK_RT_OK);
    obelisk_rt_design_type_info_v1 result{};
    EXPECT_EQ(obelisk_rt_cached_design_type_info(raw, cursor, &result),
              OBELISK_RT_OK);
    return result;
  };
  auto store = [&](obelisk_rt_design_cursor_v1 cursor,
                   const obelisk_rt_design_info_v1 &info,
                   const std::vector<uint8_t> &value,
                   const std::vector<uint8_t> &unknown) {
    uint8_t changed = 0;
    EXPECT_EQ(obelisk_rt_v1_native_state_store_plane(
                  raw, reinterpret_cast<uint8_t *>(raw->stateValue.data()),
                  execution.state_bit_count,
                  obelisk::reflection::ObjectView(
                      encoded->designDatabase.data() + cursor.offset)
                      .getStateOffset(),
                  info.bit_width, 0, value.data(), &changed),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_native_state_store_plane(
                  raw, reinterpret_cast<uint8_t *>(raw->stateUnknown.data()),
                  execution.state_bit_count,
                  obelisk::reflection::ObjectView(
                      encoded->designDatabase.data() + cursor.offset)
                      .getStateOffset(),
                  info.bit_width, 1, unknown.data(), &changed),
              OBELISK_RT_OK);
  };
  // Physical-only indexed declarations remain readable without optional
  // source semantic metadata, including the no-tag compatibility path.
  auto physicalOnly = named("top.physical_only");
  ASSERT_NE(physicalOnly, nullptr);
  auto [physicalCursor, physicalInfo] = metadata("top.physical_only");
  obelisk_rt_design_cursor_v1 missingSemantic{};
  ASSERT_EQ(obelisk_rt_cached_design_semantic_root(raw, physicalCursor,
                                                   &missingSemantic),
            OBELISK_RT_EOF);
  std::vector<uint8_t> physicalValue(4), physicalUnknown(4);
  physicalValue[0] = 5;
  store(physicalCursor, physicalInfo, physicalValue, physicalUnknown);
  auto physicalBit = keep(vpi_handle_by_index(physicalOnly, 2));
  ASSERT_NE(physicalBit, nullptr);
  EXPECT_EQ(read(physicalBit), 1);
  physicalValue[0] = 1;
  store(physicalCursor, physicalInfo, physicalValue, physicalUnknown);
  EXPECT_EQ(read(physicalBit), 0);

  // A nested member validates both enclosing tags from one coherent slice.
  auto nested = named("top.nested");
  ASSERT_NE(nested, nullptr);
  auto outerMembers = children(nested, vpiMember);
  ASSERT_EQ(outerMembers.size(), 2u);
  auto innerMembers = children(outerMembers[0], vpiMember);
  ASSERT_EQ(innerMembers.size(), 2u);
  auto [nestedCursor, nestedInfo] = metadata("top.nested");
  obelisk_rt_design_type_info_v1 nestedLayout{};
  ASSERT_EQ(obelisk_rt_cached_design_type_info(raw, {nestedInfo.type_offset},
                                               &nestedLayout),
            OBELISK_RT_OK);
  const auto innerField = field(nestedInfo, 0);
  obelisk_rt_design_type_info_v1 innerLayout{};
  ASSERT_EQ(obelisk_rt_cached_design_type_info(raw, innerField.element_type,
                                               &innerLayout),
            OBELISK_RT_OK);
  std::vector<uint8_t> nestedValue((nestedInfo.bit_width + 7) / 8),
      nestedUnknown(nestedValue.size());
  const uint64_t outerTag = nestedInfo.bit_width - nestedLayout.tag_bits;
  const uint64_t innerTag =
      innerField.packed_offset + innerLayout.bit_width - innerLayout.tag_bits;
  setBits(nestedValue, outerTag, nestedLayout.tag_bits, 1);
  setBits(nestedValue, innerTag, innerLayout.tag_bits, 1);
  setBits(nestedValue, innerField.packed_offset, 4, 6);
  store(nestedCursor, nestedInfo, nestedValue, nestedUnknown);
  EXPECT_EQ(read(innerMembers[0]), 6);
  unreadable(innerMembers[1]);
  setBits(nestedValue, innerTag, innerLayout.tag_bits, 2);
  // Unpacked tagged alternatives have disjoint, metadata-defined storage.
  obelisk_rt_design_info_v1 innerInfo{};
  innerInfo.type_offset = innerField.element_type.offset;
  setBits(nestedValue,
          innerField.packed_offset + field(innerInfo, 1).packed_offset, 4, 9);
  store(nestedCursor, nestedInfo, nestedValue, nestedUnknown);
  unreadable(innerMembers[0]);
  EXPECT_EQ(read(innerMembers[1]), 9);
  setBits(nestedValue, outerTag, nestedLayout.tag_bits, 2);
  store(nestedCursor, nestedInfo, nestedValue, nestedUnknown);
  unreadable(innerMembers[1]);
  setBits(nestedValue, outerTag, nestedLayout.tag_bits, 1);
  setBits(nestedValue, innerTag, innerLayout.tag_bits, 0);
  store(nestedCursor, nestedInfo, nestedValue, nestedUnknown);
  unreadable(innerMembers[1]);

  // A managed-container transition retains the enclosing union tag guard on
  // every Actual read, including handles selected before a later tag change.
  {
    ManagedExecutionScope lane(raw);
    ASSERT_EQ(lane.getStatus(), OBELISK_RT_OK);
    mlir::Type queueType;
    for (auto storage :
         design.getBody().front().getOps<obelisk::sim::SimStorageDeclOp>())
      if (storage.getId() == 5) {
        auto unionType =
            mlir::cast<obelisk::sim::UnpackedUnionType>(storage.getType());
        queueType =
            mlir::cast<obelisk::sim::FieldAttr>(unionType.getFields()[0])
                .getType();
      }
    ASSERT_TRUE(queueType);
    auto element = obelisk::sim::getElementDescriptor(
        mlir::cast<obelisk::sim::QueueType>(queueType).getElementType());
    ASSERT_TRUE(element);
    obelisk_rt_object_v1 *queue = nullptr;
    obelisk_rt_gc_root_v1 queueRoot{};
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane.getLane(), OBELISK_RT_CONTAINER_QUEUE, element->typeID,
                  static_cast<uint32_t>(element->kind),
                  static_cast<uint32_t>(element->flags), element->valueSize,
                  element->alignment, element->bitWidth, nullptr, 0, 0,
                  UINT64_MAX, &queue),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &queueRoot, &queue),
              OBELISK_RT_OK);
    uint64_t firstID = 2, secondID = 3;
    ASSERT_EQ(
        obelisk_rt_v1_queue_push(lane.getLane(), queue, 0, &firstID, nullptr),
        OBELISK_RT_OK);
    auto ports = named("top.ports");
    ASSERT_NE(ports, nullptr);
    auto portsMembers = children(ports, vpiMember);
    ASSERT_EQ(portsMembers.size(), 2u);
    auto [portsCursor, portsInfo] = metadata("top.ports");
    obelisk_rt_design_type_info_v1 portsLayout{};
    ASSERT_EQ(obelisk_rt_cached_design_type_info(raw, {portsInfo.type_offset},
                                                 &portsLayout),
              OBELISK_RT_OK);
    std::vector<uint8_t> portsValue((portsInfo.bit_width + 7) / 8),
        portsUnknown(portsValue.size());
    const uint64_t portsTag = portsInfo.bit_width - portsLayout.tag_bits;
    setBits(portsValue, portsTag, portsLayout.tag_bits, 1);
    setBits(portsValue, field(portsInfo, 0).packed_offset, 64,
            reinterpret_cast<uintptr_t>(queue));
    store(portsCursor, portsInfo, portsValue, portsUnknown);
    auto selected = keep(vpi_handle_by_index(portsMembers[0], 0));
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(vpi_get(vpiType, selected), vpiVirtualInterfaceVar);
    auto firstInterface = named("top.first");
    auto secondInterface = named("top.second");
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiActual, selected)),
                                  firstInterface),
              1);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_EQ(obelisk_rt_v1_container_write(lane.getLane(), queue, 0, &secondID,
                                            nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiActual, selected)),
                                  secondInterface),
              1);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    setBits(portsValue, portsTag, portsLayout.tag_bits, 2);
    store(portsCursor, portsInfo, portsValue, portsUnknown);
    EXPECT_EQ(vpi_handle(vpiActual, selected), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    setBits(portsValue, portsTag, portsLayout.tag_bits, 1);
    store(portsCursor, portsInfo, portsValue, portsUnknown);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiActual, selected)),
                                  secondInterface),
              1);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &queueRoot),
              OBELISK_RT_OK);
  }

  auto tagged = named("top.tagged");
  ASSERT_NE(tagged, nullptr);
  auto members = children(tagged, vpiMember);
  ASSERT_EQ(members.size(), 4u);
  EXPECT_EQ(vpi_get(vpiSize, tagged), 5);
  const char *names[] = {"number", "text", "record", "items"};
  for (unsigned index = 0; index != members.size(); ++index) {
    EXPECT_STREQ(vpi_get_str(vpiName, members[index]), names[index]);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiParent, members[index])),
                                  tagged),
              1);
    EXPECT_NE(keep(vpi_handle(vpiTypespec, members[index])), nullptr);
  }
  auto declaredType = keep(vpi_handle(vpiTypespec, tagged));
  EXPECT_EQ(children(declaredType, vpiTypespecMember).size(), 5u);
  auto [taggedCursor, taggedInfo] = metadata("top.tagged");
  obelisk_rt_design_type_info_v1 taggedLayout{};
  ASSERT_EQ(obelisk_rt_cached_design_type_info(raw, {taggedInfo.type_offset},
                                               &taggedLayout),
            OBELISK_RT_OK);
  std::vector<uint8_t> bytes((taggedInfo.bit_width + 7) / 8),
      unknown(bytes.size());
  auto tag = [&](unsigned ordinal) {
    setBits(bytes, taggedInfo.bit_width - taggedLayout.tag_bits,
            taggedLayout.tag_bits, ordinal);
  };
  tag(1);
  setBits(bytes, field(taggedInfo, 0).packed_offset, 32, 23);
  store(taggedCursor, taggedInfo, bytes, unknown);
  unreadable(tagged);
  EXPECT_EQ(read(members[0]), 23);
  unreadable(members[1]);
  auto bit = keep(vpi_handle_by_index(members[0], 0));
  ASSERT_NE(bit, nullptr);
  EXPECT_EQ(read(bit), 1);
  EXPECT_EQ(vpi_get(vpiSize, members[2]), 1);
  auto record = children(members[2], vpiMember);
  ASSERT_EQ(record.size(), 1u);
  unreadable(record[0]);
  tag(4);
  setBits(bytes, field(taggedInfo, 3).packed_offset, 32, 47);
  store(taggedCursor, taggedInfo, bytes, unknown);
  EXPECT_EQ(read(record[0]), 47);
  unreadable(members[0]);
  unreadable(bit);
  tag(5);
  const auto array = field(taggedInfo, 4);
  setBits(bytes, array.packed_offset, 32, 31);
  setBits(bytes, array.packed_offset + 32, 32, 41);
  store(taggedCursor, taggedInfo, bytes, unknown);
  auto first = keep(vpi_handle_by_index(members[3], 2));
  auto second = keep(vpi_handle_by_index(members[3], 1));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(read(first), 31);
  EXPECT_EQ(read(second), 41);
  unreadable(record[0]);
  // Void and uninitialized tags retain all declared variable handles, with no
  // readable payload.
  for (unsigned active : {0u, 3u}) {
    tag(active);
    store(taggedCursor, taggedInfo, bytes, unknown);
    for (auto member : {members[0], first, record[0]})
      unreadable(member);
    EXPECT_EQ(children(tagged, vpiMember).size(), 4u);
  }
  {
    ManagedExecutionScope lane(raw);
    ASSERT_EQ(lane.getStatus(), OBELISK_RT_OK);
    obelisk_rt_string_v1 string = 0;
    ASSERT_EQ(obelisk_rt_v1_string_create(lane.getLane(), "union copied string",
                                          19, &string),
              OBELISK_RT_OK);
    obelisk_rt_gc_managed_root_v1 stringRoot{};
    ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane.getLane(), &stringRoot,
                                                 &string),
              OBELISK_RT_OK);
    tag(2);
    setBits(bytes, field(taggedInfo, 1).packed_offset, 64, string);
    store(taggedCursor, taggedInfo, bytes, unknown);
    s_vpi_value text{};
    text.format = vpiStringVal;
    vpi_get_value(members[1], &text);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_STREQ(text.value.str, "union copied string");
    tag(1);
    store(taggedCursor, taggedInfo, bytes, unknown);
    text.value.str = nullptr;
    vpi_get_value(members[1], &text);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(text.value.str, nullptr);
    ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane.getLane(), &stringRoot),
              OBELISK_RT_OK);
  }
  auto packed = named("top.packed");
  EXPECT_EQ(vpi_get(vpiSize, packed), 10);
  auto packedMembers = children(packed, vpiMember);
  ASSERT_EQ(packedMembers.size(), 3u);
  auto [packedCursor, packedInfo] = metadata("top.packed");
  std::vector<uint8_t> packedValue((packedInfo.bit_width + 7) / 8),
      packedUnknown(packedValue.size());
  setBits(packedValue, 0, 8, 0x5a);
  store(packedCursor, packedInfo, packedValue, packedUnknown);
  EXPECT_EQ(read(packedMembers[0]), 0x5a);
  unreadable(packedMembers[1]);
  setBits(packedValue, 8, 2, 1);
  setBits(packedUnknown, 0, 4, 2);
  store(packedCursor, packedInfo, packedValue, packedUnknown);
  unreadable(packedMembers[0]);
  s_vpi_value vector{};
  vector.format = vpiVectorVal;
  vpi_get_value(packedMembers[1], &vector);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  ASSERT_NE(vector.value.vector, nullptr);
  EXPECT_EQ(vector.value.vector[0].aval, 8u);
  EXPECT_EQ(vector.value.vector[0].bval, 2u);
  setBits(packedUnknown, 8, 2, 1);
  store(packedCursor, packedInfo, packedValue, packedUnknown);
  unreadable(packedMembers[1]);
  auto overlap = named("top.overlap");
  EXPECT_EQ(vpi_get(vpiSize, overlap), 2);
  auto overlapped = children(overlap, vpiMember);
  ASSERT_EQ(overlapped.size(), 2u);
  auto [overlapCursor, overlapInfo] = metadata("top.overlap");
  std::vector<uint8_t> overlapValue(8), overlapUnknown(8);
  double real = 1.25;
  std::memcpy(overlapValue.data(), &real, 8);
  store(overlapCursor, overlapInfo, overlapValue, overlapUnknown);
  s_vpi_value realValue{};
  realValue.format = vpiRealVal;
  vpi_get_value(overlapped[1], &realValue);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_DOUBLE_EQ(realValue.value.real, real);
  unreadable(overlap);
  // The same field recipe follows live automatic, object-offset and path
  // aliases.
  for (unsigned backing = 0; backing != 3; ++backing) {
    SCOPED_TRACE(backing);
    uint64_t activation = 0;
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(raw, 45, OBELISK_RT_VPI_FRAME_CALL,
                                            20, &activation),
              OBELISK_RT_OK);
    auto frame = vpi_handle(vpiFrame, nullptr);
    auto local = vpi_handle_by_name(const_cast<char *>("small"), frame);
    ASSERT_NE(local, nullptr);
    ManagedExecutionScope lane(raw);
    ASSERT_EQ(lane.getStatus(), OBELISK_RT_OK);
    uint64_t reference = 0;
    obelisk_rt_object_v1 *owner = nullptr, *queue = nullptr;
    obelisk_rt_gc_root_v1 ownerRoot{}, queueRoot{};
    uint8_t data[2] = {5, 1};
    auto localDecl = mlir::cast<obelisk::sim::SimVPILocalDeclOp>(
        mlir::SymbolTable::lookupSymbolIn(design, "small"));
    auto element = obelisk::sim::getElementDescriptor(localDecl.getValueType());
    ASSERT_TRUE(element);
    const obelisk_rt_trace_layout_v1 layout{
        OBELISK_RT_VERSION, 0, 16, 8, nullptr, 0};
    obelisk_rt_class_descriptor_v1 descriptor{};
    descriptor.version = OBELISK_RT_VERSION;
    descriptor.class_id = 100;
    descriptor.instance_size = 16;
    descriptor.instance_alignment = 8;
    descriptor.layout = &layout;
    if (!backing) {
      ASSERT_EQ(
          obelisk_rt_v1_native_state_alloc(raw, 10, data, nullptr, &reference),
          OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind(raw, 1, reference), OBELISK_RT_OK);
    } else if (backing == 1) {
      ASSERT_EQ(
          obelisk_rt_v1_object_allocate(lane.getLane(), &descriptor, &owner),
          OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &ownerRoot, &owner),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_object_write(owner, 8, data, 2), OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 1, owner, 8, 1),
                OBELISK_RT_OK);
    } else {
      ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                    lane.getLane(), OBELISK_RT_CONTAINER_QUEUE, element->typeID,
                    static_cast<uint32_t>(element->kind),
                    static_cast<uint32_t>(element->flags), element->valueSize,
                    element->alignment, element->bitWidth, nullptr, 0, 0,
                    UINT64_MAX, &queue),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &queueRoot, &queue),
                OBELISK_RT_OK);
      ASSERT_EQ(
          obelisk_rt_v1_queue_push(lane.getLane(), queue, 0, data, nullptr),
          OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                    lane.getLane(), queue, 0, nullptr, 0, 0, nullptr, nullptr,
                    0, &owner),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane.getLane(), &ownerRoot, &owner),
                OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_vpi_frame_bind_alias(raw, 1, owner, 0, 2),
                OBELISK_RT_OK);
    }
    auto selected = children(local, vpiMember);
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(read(selected[0]), 5);
    unreadable(selected[1]);
    data[0] = 0x95;
    data[1] = 2;
    if (!backing) {
      uint8_t changed = 0;
      ASSERT_EQ(obelisk_rt_v1_native_state_store_plane(
                    raw, reinterpret_cast<uint8_t *>(raw->stateValue.data()),
                    execution.state_bit_count, reference, 10, 0, data,
                    &changed),
                OBELISK_RT_OK);
    } else if (backing == 1)
      ASSERT_EQ(obelisk_rt_v1_object_write(owner, 8, data, 2), OBELISK_RT_OK);
    else
      ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane.getLane(), owner, data,
                                                   nullptr),
                OBELISK_RT_OK);
    unreadable(selected[0]);
    EXPECT_EQ(read(selected[1]), 9);
    EXPECT_EQ(
        vpi_compare_objects(keep(vpi_handle(vpiParent, selected[1])), local),
        1);
    if (!backing)
      ASSERT_EQ(obelisk_rt_v1_native_state_release(raw, reference, 0),
                OBELISK_RT_OK);
    else {
      ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &ownerRoot),
                OBELISK_RT_OK);
      if (backing == 2)
        ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane.getLane(), &queueRoot),
                  OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_gc_collect(lane.getLane()), OBELISK_RT_OK);
    }
    unreadable(selected[1]);
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, activation), OBELISK_RT_OK);
    EXPECT_EQ(vpi_get(vpiType, local), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(raw, root), OBELISK_RT_OK);
  // Static declaration and member handles survive unrelated activation expiry.
  EXPECT_EQ(vpi_get(vpiType, members[0]), vpiIntVar);
  runtime.reset();
}
} // namespace
