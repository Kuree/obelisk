//===- GeneratedVPIUnionMessagesTest.cpp - Copied union variable views
//-----===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, MailboxUnionMessages) {
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
  options.vpi = "full";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION,
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
  talos_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(talos_rt_v1_gc_lane_create(runtime, &lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_enter(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  const std::array<const char *, 3> names{"top.tagged", "top.plain",
                                          "top.packed"};
  const std::array<const char *, 3> symbols{"tagged", "plain", "packed"};
  std::array<talos_rt_object_v1 *, 3> objects{};
  talos_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            TALOS_RT_OK);
  std::array<talos_rt_design_cursor_v1, 3> storage{}, physical{};
  std::array<talos_rt_design_type_info_v1, 3> types{};
  std::array<talos::sim::ElementDescriptor, 3> descriptors{};
  for (size_t index = 0; index != names.size(); ++index) {
    auto declaration =
        mlir::dyn_cast_or_null<talos::sim::SimVPITypespecDeclOp>(
            mlir::SymbolTable::lookupSymbolIn(design, symbols[index]));
    ASSERT_TRUE(declaration);
    auto descriptor = talos::sim::getElementDescriptor(
        declaration.getMessageTypeAttr().getValue());
    ASSERT_TRUE(descriptor);
    descriptors[index] = *descriptor;
    std::vector<talos_rt_element_trace_slot_v1> slots;
    for (size_t slot = 0; slot != descriptor->traceOffsets.size(); ++slot)
      slots.push_back({static_cast<uint64_t>(descriptor->traceOffsets[slot]),
                       static_cast<uint32_t>(descriptor->traceKinds[slot]), 0});
    ASSERT_EQ(
        talos_rt_v1_mailbox_create_typed(
            lane, descriptor->typeID, static_cast<uint32_t>(descriptor->kind),
            static_cast<uint32_t>(descriptor->flags), descriptor->valueSize,
            descriptor->alignment, descriptor->bitWidth, slots.data(),
            slots.size(), 0, &objects[index]),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(names[index]),
                  std::strlen(names[index]), &storage[index]),
              TALOS_RT_OK);
    talos_rt_design_cursor_v1 semantic{}, owner{};
    ASSERT_EQ(talos_rt_cached_design_semantic_root(runtime, storage[index],
                                                     &semantic),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_vpi_semantic_class_typespec(runtime, semantic,
                                                            &owner),
              TALOS_RT_OK);
    talos_rt_design_semantic_type_edge_v1 edge{};
    ASSERT_EQ(talos_rt_cached_design_semantic_type_edge(runtime, semantic, 0,
                                                          &edge),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_vpi_mailbox_element_type(
                  runtime, owner, edge.child, &physical[index]),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_cached_design_type_info(runtime, physical[index],
                                                 &types[index]),
              TALOS_RT_OK);
    const uint64_t word = talos_rt_managed_word_from_object(objects[index]);
    ASSERT_EQ(
        talos_rt_v1_design_write(runtime, storage[index], &word, nullptr, 64),
        TALOS_RT_OK);
  }
  auto field = [&](talos_rt_design_cursor_v1 type, size_t ordinal) {
    talos_rt_design_cursor_v1 cursor{};
    talos_rt_design_type_info_v1 result{};
    EXPECT_EQ(
        talos_rt_cached_design_type_child(runtime, type, ordinal, &cursor),
        TALOS_RT_OK);
    EXPECT_EQ(talos_rt_cached_design_type_info(runtime, cursor, &result),
              TALOS_RT_OK);
    return result;
  };
  std::array<talos_rt_design_type_info_v1, 5> taggedFields{};
  for (size_t index = 0; index != taggedFields.size(); ++index)
    taggedFields[index] = field(physical[0], index);
  const auto recordX = field(taggedFields[3].element_type, 0);
  talos_rt_design_type_info_v1 array{};
  ASSERT_EQ(talos_rt_cached_design_type_info(
                runtime, taggedFields[4].element_type, &array),
            TALOS_RT_OK);
  ASSERT_EQ(array.range_left, 2);
  ASSERT_EQ(array.range_right, 1);
  const uint64_t arrayStride = array.bit_width / 2;
  const auto plainBits = field(physical[1], 0);
  const auto plainReal = field(physical[1], 1);
  EXPECT_EQ(plainBits.packed_offset, plainReal.packed_offset);
  ASSERT_EQ(types[0].tag_bits, 3u);
  ASSERT_EQ(types[1].tag_bits, 0u);
  ASSERT_EQ(types[2].tag_bits, 2u);
  auto putBits = [](std::vector<uint8_t> &bytes, uint64_t offset,
                    unsigned width, uint64_t value) {
    EXPECT_LE(offset + width, bytes.size() * 8);
    for (unsigned bit = 0; bit != width; ++bit)
      if ((value >> bit) & 1)
        bytes[(offset + bit) / 8] |= uint8_t{1} << ((offset + bit) % 8);
  };
  auto put = [&](size_t mailbox, const std::vector<uint8_t> &value,
                 const std::vector<uint8_t> *unknown = nullptr) {
    uint32_t success = 0;
    EXPECT_EQ(talos_rt_v1_mailbox_try_put(
                  lane, objects[mailbox], value.data(),
                  unknown ? unknown->data() : nullptr, &success),
              TALOS_RT_OK);
    EXPECT_EQ(success, 1u);
  };
  talos_rt_string_v1 stringWord = 0;
  talos_rt_gc_managed_root_v1 stringRoot{};
  ASSERT_EQ(talos_rt_v1_gc_managed_root_push(lane, &stringRoot, &stringWord),
            TALOS_RT_OK);
  const std::string text = "a copied tagged union string survives collection";
  ASSERT_EQ(
      talos_rt_v1_string_create(lane, text.data(), text.size(), &stringWord),
      TALOS_RT_OK);
  for (unsigned active = 0; active != 6; ++active) {
    std::vector<uint8_t> value(descriptors[0].valueSize, 0);
    // Unpacked tagged unions reserve zero for an uninitialized tag; source
    // ordinals use tag ordinal+1. The last snapshot has no active member.
    putBits(value, types[0].bit_width - types[0].tag_bits, types[0].tag_bits,
            active < 5 ? active + 1 : 0);
    if (active == 0)
      putBits(value, taggedFields[0].packed_offset, 32,
              static_cast<uint32_t>(-23));
    if (active == 1)
      putBits(value, taggedFields[1].packed_offset, 64, stringWord);
    if (active == 3)
      putBits(value, taggedFields[3].packed_offset + recordX.packed_offset, 32,
              51);
    if (active == 4) {
      putBits(value, taggedFields[4].packed_offset, 32, 71);
      putBits(value, taggedFields[4].packed_offset + arrayStride, 32, 72);
    }
    put(0, value);
  }
  const double real = 3.25;
  uint64_t realBits = 0;
  std::memcpy(&realBits, &real, sizeof(real));
  std::vector<uint8_t> plain(descriptors[1].valueSize, 0);
  putBits(plain, plainReal.packed_offset, 64, realBits);
  put(1, plain);
  const std::array<uint64_t, 4> packedValue{0xa9, 0x10b, 0x201, 0};
  const std::array<uint64_t, 4> packedUnknown{0x0c, 0x004, 0, 0x300};
  for (size_t index = 0; index != packedValue.size(); ++index) {
    std::vector<uint8_t> value(descriptors[2].valueSize, 0);
    std::vector<uint8_t> unknown(value.size(), 0);
    putBits(value, 0, 10, packedValue[index]);
    putBits(unknown, 0, 10, packedUnknown[index]);
    put(2, value, &unknown);
  }
  std::vector<vpiHandle> retained;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      retained.push_back(handle);
    return handle;
  };
  std::array<vpiHandle, 3> mailboxes{}, pending{};
  std::array<std::vector<vpiHandle>, 3> messages;
  for (size_t index = 0; index != names.size(); ++index) {
    auto variable =
        keep(vpi_handle_by_name(const_cast<char *>(names[index]), nullptr));
    mailboxes[index] = vpi_handle(vpiClassObj, variable);
    ASSERT_NE(mailboxes[index], nullptr);
    pending[index] = vpi_iterate(vpiMessages, mailboxes[index]);
    ASSERT_NE(pending[index], nullptr);
    auto iterator = vpi_iterate(vpiMessages, mailboxes[index]);
    ASSERT_NE(iterator, nullptr);
    while (auto message = vpi_scan(iterator))
      messages[index].push_back(keep(message));
    EXPECT_EQ(messages[index].size(), index == 0 ? 6u : index == 1 ? 1u : 4u);
    const uint64_t zero = 0;
    ASSERT_EQ(
        talos_rt_v1_design_write(runtime, storage[index], &zero, nullptr, 64),
        TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_gc_managed_root_pop(lane, &stringRoot),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_range_pop(lane, &roots), TALOS_RT_OK);
  objects.fill(nullptr);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(runtime, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  for (size_t index = 0; index != names.size(); ++index) {
    EXPECT_EQ(vpi_get(vpiType, mailboxes[index]), vpiUndefined);
    EXPECT_EQ(vpi_get(vpiType, pending[index]), vpiUndefined);
  }
  auto children = [&](vpiHandle source, PLI_INT32 selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, source);
    EXPECT_NE(iterator, nullptr);
    if (!iterator)
      return result;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, source), 1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto denied = [](vpiHandle source) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(source, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  };
  auto integer = [](vpiHandle source) {
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(source, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.integer;
  };
  auto binary = [](uint32_t value) {
    std::string result = "32'sb";
    for (unsigned bit = 32; bit != 0; --bit)
      result += value & (uint32_t{1} << (bit - 1)) ? '1' : '0';
    return result;
  };
  const std::array<const char *, 4> memberNames{"number", "text", "record",
                                                "items"};
  const std::array<PLI_INT32, 4> memberKinds{vpiIntVar, vpiStringVar,
                                             vpiStructVar, vpiRegArray};
  const std::array<PLI_INT32, 4> memberTypes{
      vpiIntTypespec, vpiStringTypespec, vpiStructTypespec, vpiArrayTypespec};
  vpiHandle survivor = nullptr;
  for (size_t active = 0; active != messages[0].size(); ++active) {
    auto message = messages[0][active];
    EXPECT_EQ(vpi_get(vpiType, message), vpiUnionVar);
    EXPECT_EQ(vpi_get(vpiTagged, keep(vpi_handle(vpiTypespec, message))), 1);
    EXPECT_EQ(vpi_get(vpiPacked, message), 0);
    EXPECT_EQ(vpi_get(vpiSize, message), 5);
    denied(message);
    auto type = keep(vpi_handle(vpiTypespec, message));
    EXPECT_EQ(vpi_get(vpiType, type), vpiUnionTypespec);
    auto declarations = children(type, vpiTypespecMember);
    ASSERT_EQ(declarations.size(), 5u);
    EXPECT_STREQ(vpi_get_str(vpiName, declarations[2]), "none");
    auto voidType = keep(vpi_handle(vpiTypespec, declarations[2]));
    EXPECT_EQ(vpi_get(vpiType, voidType), vpiVoidTypespec);
    auto members = children(message, vpiMember);
    ASSERT_EQ(members.size(), 4u); // The void tag has no variable or payload.
    for (size_t index = 0; index != members.size(); ++index) {
      auto member = members[index];
      EXPECT_EQ(vpi_get(vpiType, member), memberKinds[index]);
      EXPECT_STREQ(vpi_get_str(vpiName, member), memberNames[index]);
      EXPECT_EQ(vpi_get(vpiStructUnionMember, member), 1);
      auto parent = keep(vpi_handle(vpiParent, member));
      EXPECT_EQ(vpi_compare_objects(parent, message), 1);
      auto memberType = keep(vpi_handle(vpiTypespec, member));
      EXPECT_EQ(vpi_get(vpiType, memberType), memberTypes[index]);
    }
    if (active == 0)
      EXPECT_EQ(integer(members[0]), -23);
    else
      denied(members[0]);
    if (active == 1) {
      s_vpi_value value{};
      value.format = vpiStringVal;
      vpi_get_value(members[1], &value);
      ASSERT_NE(value.value.str, nullptr);
      EXPECT_EQ(std::string(value.value.str), text);
      EXPECT_EQ(vpi_get(vpiSize, members[1]),
                static_cast<PLI_INT32>(text.size()));
    } else
      denied(members[1]);
    auto recordMembers = children(members[2], vpiMember);
    ASSERT_EQ(recordMembers.size(), 1u);
    EXPECT_EQ(vpi_get(vpiType, recordMembers[0]), vpiIntVar);
    if (active == 3) {
      EXPECT_EQ(integer(recordMembers[0]), 51);
      survivor = recordMembers[0];
    } else
      denied(recordMembers[0]);
    auto left = keep(vpi_handle(vpiLeftRange, members[3]));
    auto right = keep(vpi_handle(vpiRightRange, members[3]));
    EXPECT_EQ(integer(left), 2);
    EXPECT_EQ(integer(right), 1);
    auto ranges = children(members[3], vpiRange);
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, ranges[0]))), 2);
    EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, ranges[0]))), 1);
    auto arrayElements = children(members[3], vpiReg);
    ASSERT_EQ(arrayElements.size(), 2u);
    for (PLI_INT32 index : {2, 1}) {
      auto element = keep(vpi_handle_by_index(members[3], index));
      EXPECT_EQ(vpi_get(vpiType, element), vpiIntVar);
      EXPECT_EQ(vpi_compare_objects(element, arrayElements[2 - index]), 1);
      EXPECT_EQ(vpi_get(vpiArrayMember, element), 1);
      auto indices = children(element, vpiIndex);
      ASSERT_EQ(indices.size(), 1u);
      EXPECT_EQ(integer(indices.front()), index);
      auto parent = keep(vpi_handle(vpiParent, element));
      EXPECT_EQ(vpi_compare_objects(parent, members[3]), 1);
      if (active == 4)
        EXPECT_EQ(integer(element), index == 2 ? 71 : 72);
      else
        denied(element);
    }
    if (active == 5) {
      EXPECT_EQ(vpi_get_str(vpiDecompile, message), nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    } else {
      const char *rendered = vpi_get_str(vpiDecompile, message);
      ASSERT_NE(rendered, nullptr);
      const std::string expression(rendered);
      const std::array<const char *, 5> fields{"number", "text", "none",
                                               "record", "items"};
      EXPECT_EQ(expression.find(std::string("tagged \\") + fields[active]), 0u);
      if (active == 0)
        EXPECT_NE(expression.find(binary(static_cast<uint32_t>(-23))),
                  std::string::npos);
      if (active == 1)
        EXPECT_NE(expression.find('"' + text + '"'), std::string::npos);
      if (active == 3)
        EXPECT_NE(expression.find("'{" + binary(51)), std::string::npos);
      if (active == 4) {
        const auto first = expression.find(binary(71));
        const auto second = expression.find(binary(72));
        ASSERT_NE(first, std::string::npos);
        ASSERT_NE(second, std::string::npos);
        EXPECT_LT(first, second);
        EXPECT_NE(expression.find("'{"), std::string::npos);
      }
    }
    s_vpi_value write{};
    write.format = vpiIntVal;
    EXPECT_EQ(vpi_put_value(members[0], &write, nullptr, vpiNoDelay), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  }
  auto plainMessage = messages[1][0];
  EXPECT_EQ(vpi_get(vpiType, plainMessage), vpiUnionVar);
  EXPECT_EQ(vpi_get(vpiTagged, keep(vpi_handle(vpiTypespec, plainMessage))), 0);
  EXPECT_EQ(vpi_get(vpiPacked, plainMessage), 0);
  denied(plainMessage);
  EXPECT_EQ(vpi_get_str(vpiDecompile, plainMessage), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  auto overlap = children(plainMessage, vpiMember);
  ASSERT_EQ(overlap.size(), 2u);
  EXPECT_EQ(vpi_get(vpiType, overlap[0]), vpiLongIntVar);
  EXPECT_EQ(vpi_get(vpiType, overlap[1]), vpiRealVar);
  s_vpi_value bits{};
  bits.format = vpiVectorVal;
  vpi_get_value(overlap[0], &bits);
  ASSERT_NE(bits.value.vector, nullptr);
  EXPECT_EQ(static_cast<uint32_t>(bits.value.vector[0].aval),
            uint32_t(realBits));
  EXPECT_EQ(static_cast<uint32_t>(bits.value.vector[1].aval),
            uint32_t(realBits >> 32));
  s_vpi_value number{};
  number.format = vpiRealVal;
  vpi_get_value(overlap[1], &number);
  EXPECT_DOUBLE_EQ(number.value.real, real);
  for (size_t active = 0; active != messages[2].size(); ++active) {
    auto message = messages[2][active];
    EXPECT_EQ(vpi_get(vpiType, message), vpiUnionVar);
    EXPECT_EQ(vpi_get(vpiTagged, keep(vpi_handle(vpiTypespec, message))), 1);
    EXPECT_EQ(vpi_get(vpiPacked, message), 1);
    EXPECT_EQ(vpi_get(vpiSize, message), 10);
    bits.format = vpiVectorVal;
    vpi_get_value(message, &bits);
    ASSERT_NE(bits.value.vector, nullptr);
    EXPECT_EQ(bits.value.vector[0].aval,
              packedValue[active] ^ packedUnknown[active]);
    EXPECT_EQ(bits.value.vector[0].bval, packedUnknown[active]);
    if (active == 3) {
      EXPECT_EQ(vpi_get_str(vpiDecompile, message), nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    }
    auto members = children(message, vpiMember);
    ASSERT_EQ(members.size(), 3u);
    for (size_t index = 0; index != members.size(); ++index) {
      auto member = members[index];
      EXPECT_EQ(vpi_get(vpiType, member), index == 2 ? vpiBitVar : vpiReg);
      if (index != active) {
        denied(member);
      } else {
        bits.format = vpiVectorVal;
        vpi_get_value(member, &bits);
        ASSERT_NE(bits.value.vector, nullptr);
        const uint64_t mask = index == 0 ? 0xff : index == 1 ? 0xf : 3;
        EXPECT_EQ(bits.value.vector[0].aval,
                  (packedValue[active] ^ packedUnknown[active]) & mask);
        EXPECT_EQ(bits.value.vector[0].bval, packedUnknown[active] & mask);
      }
      auto ranges = children(member, vpiRange);
      ASSERT_EQ(ranges.size(), index == 2 ? 2u : 1u);
      const PLI_INT32 left = index == 0 ? 7 : index == 1 ? 3 : 0;
      const PLI_INT32 right = index == 2 ? 1 : 0;
      EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, ranges[0]))), left);
      EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, ranges[0]))), right);
      if (index == 2) {
        EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, ranges[1]))), 0);
        EXPECT_EQ(integer(keep(vpi_handle(vpiRightRange, ranges[1]))), 0);
      }
      auto selectedBits = children(member, vpiBit);
      const size_t width = index == 0 ? 8 : index == 1 ? 4 : 2;
      ASSERT_EQ(selectedBits.size(), width);
      for (size_t ordinal = 0; ordinal != width; ++ordinal) {
        const PLI_INT32 sourceIndex = index == 2 ? ordinal : left - ordinal;
        auto selected = keep(vpi_handle_by_index(member, sourceIndex));
        if (index == 2) {
          // A singleton packed dimension remains an array until selected.
          EXPECT_EQ(vpi_get(vpiType, selected), vpiBitVar);
          EXPECT_EQ(vpi_get(vpiSize, selected), 1);
          EXPECT_EQ(vpi_get(vpiVector, selected), 1);
          EXPECT_EQ(vpi_get(vpiScalar, selected), 0);
          auto remainingRanges = children(selected, vpiRange);
          ASSERT_EQ(remainingRanges.size(), 1u);
          EXPECT_EQ(integer(keep(vpi_handle(vpiLeftRange, remainingRanges[0]))),
                    0);
          EXPECT_EQ(
              integer(keep(vpi_handle(vpiRightRange, remainingRanges[0]))), 0);
          selected = keep(vpi_handle_by_index(selected, 0));
          PLI_INT32 indices[] = {sourceIndex, 0};
          auto repeated = keep(vpi_handle_by_multi_index(member, 2, indices));
          EXPECT_EQ(vpi_compare_objects(selected, repeated), 1);
        } else {
          auto repeated = keep(vpi_handle_by_index(member, sourceIndex));
          EXPECT_EQ(vpi_compare_objects(selected, repeated), 1);
          EXPECT_EQ(integer(keep(vpi_handle(vpiIndex, selected))), sourceIndex);
        }
        EXPECT_EQ(vpi_get(vpiType, selected), vpiRegBit);
        EXPECT_EQ(vpi_get(vpiSize, selected), 1);
        EXPECT_EQ(vpi_compare_objects(selected, selectedBits[ordinal]), 1);
        auto parent = keep(vpi_handle(vpiParent, selected));
        EXPECT_EQ(vpi_compare_objects(parent, member), 1);
        auto indices = children(selected, vpiIndex);
        ASSERT_EQ(indices.size(), index == 2 ? 2u : 1u);
        EXPECT_EQ(integer(indices.front()), index == 2 ? 0 : sourceIndex);
        if (index == 2)
          EXPECT_EQ(integer(indices[1]), sourceIndex);
        if (index != active) {
          denied(selected);
        } else {
          bits.format = vpiVectorVal;
          vpi_get_value(selected, &bits);
          ASSERT_NE(bits.value.vector, nullptr);
          const size_t shift = width - ordinal - 1;
          EXPECT_EQ(bits.value.vector[0].aval,
                    ((packedValue[active] ^ packedUnknown[active]) >> shift) &
                        1);
          EXPECT_EQ(bits.value.vector[0].bval,
                    (packedUnknown[active] >> shift) & 1);
        }
      }
    }
  }
  ASSERT_NE(survivor, nullptr);
  for (auto handle : retained)
    if (handle != survivor)
      EXPECT_EQ(vpi_release_handle(handle), 1);
  // A descendant retains its copied owner graph after the original handles
  // are released, so Parent can reconstruct the exact struct and union views.
  EXPECT_EQ(integer(survivor), 51);
  auto parent = vpi_handle(vpiParent, survivor);
  ASSERT_NE(parent, nullptr);
  EXPECT_EQ(vpi_get(vpiType, parent), vpiStructVar);
  auto unionParent = vpi_handle(vpiParent, parent);
  ASSERT_NE(unionParent, nullptr);
  EXPECT_EQ(vpi_get(vpiType, unionParent), vpiUnionVar);
  EXPECT_EQ(vpi_release_handle(unionParent), 1);
  EXPECT_EQ(vpi_release_handle(parent), 1);
  EXPECT_EQ(vpi_release_handle(survivor), 1);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
