//===- GeneratedVPIClassMessagesTest.cpp - Mailbox VPI snapshots ----------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "talos/Analysis/ManagedClassLayoutAnalysis.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "talos/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "llvm/IR/DataLayout.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace talos::reflection;

TEST(GeneratedDesignDatabase, MailboxMessages) {
  const char *inputPath = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(inputPath, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  talos::sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(inputPath, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<talos::sim::SimDesignOp>().begin();
  auto layouts = talos::analysis::ManagedClassLayoutAnalysis::compute(
      design,
      llvm::DataLayout((*module)
                           ->getAttrOfType<mlir::StringAttr>("llvm.data_layout")
                           .getValue()));
  ASSERT_TRUE(mlir::succeeded(layouts));
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

  std::array<talos_rt_class_descriptor_v1, 3> descriptors{};
  std::array<talos_rt_trace_layout_v1, 3> traces{};
  std::array<talos_rt_trace_entry_v1, 2> entries{};
  for (size_t i = 0; i != descriptors.size(); ++i) {
    const auto *layout = layouts->lookup(i + 1);
    ASSERT_NE(layout, nullptr);
    auto &descriptor = descriptors[i];
    traces[i] = {TALOS_RT_VERSION,
                 0,
                 layout->size,
                 layout->alignment,
                 i ? &entries[i - 1] : nullptr,
                 i ? 1u : 0u};
    descriptor.version = TALOS_RT_VERSION;
    descriptor.class_id = i + 1;
    descriptor.instance_size = layout->size;
    descriptor.instance_alignment = layout->alignment;
    descriptor.layout = &traces[i];
    if (i == 1) {
      ASSERT_TRUE(layout->builtinBaseOffset);
      descriptor.builtin_base_kind = TALOS_RT_BUILTIN_CLASS_MAILBOX;
      descriptor.builtin_base_offset = *layout->builtinBaseOffset;
      entries[0] = {
          *layout->builtinBaseOffset,      0,      1, TALOS_RT_TRACE_STRONG,
          TALOS_RT_MANAGED_SLOT_MAILBOX, nullptr};
    } else if (i == 2) {
      descriptor.base = &descriptors[1];
      entries[1] = {0, 0, 1, TALOS_RT_TRACE_EMBEDDED, 0, &traces[1]};
    }
    ASSERT_EQ(talos_rt_v1_class_register(runtime, &descriptor),
              TALOS_RT_OK);
  }

  std::array<talos_rt_object_v1 *, 10> objects{};
  talos_rt_gc_root_range_v1 roots{};
  ASSERT_EQ(talos_rt_v1_gc_root_range_push(lane, &roots, objects.data(),
                                             objects.size()),
            TALOS_RT_OK);
  struct Element {
    uint32_t kind, flags;
    uint64_t bytes, alignment, bits;
  };
  const std::array<Element, 7> elements{{
      {TALOS_RT_ELEMENT_LOGIC, TALOS_RT_ELEMENT_FOUR_STATE, 17, 1, 130},
      {TALOS_RT_ELEMENT_REAL, 0, 8, 8, 64},
      {TALOS_RT_ELEMENT_STRING, 0, 8, 8, 0},
      {TALOS_RT_ELEMENT_CLASS_HANDLE, 0, 8, 8, 0},
      {TALOS_RT_ELEMENT_CONTAINER_HANDLE, 0, 8, 8, 0},
      {TALOS_RT_ELEMENT_REAL, 0, 4, 4, 32},
      {TALOS_RT_ELEMENT_BITS, TALOS_RT_ELEMENT_SIGNED, 4, 4, 32},
  }};
  const talos_rt_element_trace_slot_v1 semaphoreSlot{
      0, TALOS_RT_MANAGED_SLOT_SEMAPHORE, 0};
  for (size_t i = 0; i != elements.size(); ++i) {
    const Element &element = elements[i];
    ASSERT_EQ(talos_rt_v1_mailbox_create_typed(
                  lane, 100 + i, element.kind, element.flags, element.bytes,
                  element.alignment, element.bits,
                  i == 4 ? &semaphoreSlot : nullptr, i == 4 ? 1 : 0, 0,
                  &objects[i ? i : 9]),
              TALOS_RT_OK);
  }
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &descriptors[0], &objects[7]),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_object_allocate(lane, &descriptors[2], &objects[0]),
            TALOS_RT_OK);
  const uint64_t payloadWord = talos_rt_managed_word_from_object(objects[9]);
  ASSERT_EQ(talos_rt_v1_object_write(objects[0],
                                       descriptors[1].builtin_base_offset,
                                       &payloadWord, sizeof(payloadWord)),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_semaphore_create(lane, 1, &objects[8]),
            TALOS_RT_OK);
  talos_rt_string_v1 string = 0;
  talos_rt_gc_managed_root_v1 stringRoot{};
  ASSERT_EQ(talos_rt_v1_gc_managed_root_push(lane, &stringRoot, &string),
            TALOS_RT_OK);
  const std::string text = "snapshot survives mailbox collection\n\"quoted\"";
  ASSERT_EQ(
      talos_rt_v1_string_create(lane, text.data(), text.size(), &string),
      TALOS_RT_OK);
  std::array<uint8_t, 17> firstValue{}, firstUnknown{}, secondValue{};
  firstValue[0] = 0x12;
  firstUnknown[0] = 0x18; // Internal X=(0,1), Z=(1,1).
  firstValue[16] = 0x02;
  secondValue[8] = 0x80;
  const double real = 3.25;
  const float shortReal = -1.5f;
  const uint64_t classWord = talos_rt_managed_word_from_object(objects[7]);
  const uint64_t semaphoreWord =
      talos_rt_managed_word_from_object(objects[8]);
  const uint64_t nullWord = 0;
  auto put = [&](size_t index, const void *value,
                 const void *unknown = nullptr) {
    uint32_t success = 0;
    EXPECT_EQ(talos_rt_v1_mailbox_try_put(lane, objects[index], value,
                                            unknown, &success),
              TALOS_RT_OK);
    EXPECT_EQ(success, 1u);
  };
  put(0, firstValue.data(), firstUnknown.data());
  put(0, secondValue.data(), std::array<uint8_t, 17>{}.data());
  put(1, &real);
  put(1, &real);
  put(2, &string);
  put(3, &classWord);
  put(3, &nullWord);
  put(4, &semaphoreWord);
  put(5, &shortReal);
  const int32_t signedWord = -17;
  put(6, &signedWord);

  std::vector<vpiHandle> persistent;
  auto keep = [&](vpiHandle handle) {
    EXPECT_NE(handle, nullptr);
    if (handle)
      persistent.push_back(handle);
    return handle;
  };
  std::array<talos_rt_design_cursor_v1, 7> storage{};
  std::array<vpiHandle, 7> variables{}, mailboxes{};
  const std::array<const char *, 7> names{
      "top.wide",      "top.real",       "top.string", "top.object",
      "top.semaphore", "top.short_real", "top.word"};
  for (size_t i = 0; i != names.size(); ++i) {
    ASSERT_EQ(talos_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(names[i]),
                  std::strlen(names[i]), &storage[i]),
              TALOS_RT_OK);
    ObjectView object(encoded->designDatabase.data() + storage[i].offset);
    ASSERT_EQ(talos_rt_v1_gc_design_candidate_root_register(
                  runtime, object.getStateOffset(),
                  TALOS_RT_MANAGED_ROOT_KIND_CLASS |
                      TALOS_RT_MANAGED_ROOT_KIND_CONTAINER),
              TALOS_RT_OK);
    uint64_t word = talos_rt_managed_word_from_object(objects[i]);
    ASSERT_EQ(
        talos_rt_v1_design_write(runtime, storage[i], &word, nullptr, 64),
        TALOS_RT_OK);
    variables[i] =
        keep(vpi_handle_by_name(const_cast<char *>(names[i]), nullptr));
    mailboxes[i] = vpi_handle(vpiClassObj, variables[i]);
    ASSERT_NE(mailboxes[i], nullptr);
  }
  ASSERT_EQ(talos_rt_v1_gc_managed_root_pop(lane, &stringRoot),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_root_range_pop(lane, &roots), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);

  auto iterator = [&](size_t index) {
    vpiHandle result = vpi_iterate(vpiMessages, mailboxes[index]);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(vpi_get(vpiType, result), vpiIterator);
    EXPECT_EQ(vpi_get(vpiIteratorType, result), vpiMessages);
    vpiHandle use = vpi_handle(vpiUse, result);
    EXPECT_NE(use, nullptr);
    EXPECT_EQ(vpi_compare_objects(use, mailboxes[index]), 1);
    if (use)
      EXPECT_EQ(vpi_release_handle(use), 1);
    return result;
  };
  auto single = [&](size_t index) {
    vpiHandle items = iterator(index);
    vpiHandle message = keep(vpi_scan(items));
    EXPECT_EQ(vpi_scan(items), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return message;
  };
  vpiHandle wideIterator = iterator(0);
  vpiHandle first = keep(vpi_scan(wideIterator));
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(vpi_get(vpiType, first), vpiConstant);
  EXPECT_EQ(vpi_get(vpiConstType, first), vpiBinaryConst);
  EXPECT_EQ(vpi_get(vpiSize, first), 130);
  auto checkFirst = [&] {
    s_vpi_value value{};
    value.format = vpiVectorVal;
    vpi_get_value(first, &value);
    ASSERT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_NE(value.value.vector, nullptr);
    EXPECT_EQ(static_cast<uint32_t>(value.value.vector[0].aval), 0x0au);
    EXPECT_EQ(static_cast<uint32_t>(value.value.vector[0].bval), 0x18u);
    for (size_t i = 1; i != 4; ++i) {
      EXPECT_EQ(value.value.vector[i].aval, 0);
      EXPECT_EQ(value.value.vector[i].bval, 0);
    }
    EXPECT_EQ(value.value.vector[4].aval, 2);
    EXPECT_EQ(value.value.vector[4].bval, 0);
  };
  checkFirst();
  const char *wideLiteral = vpi_get_str(vpiDecompile, first);
  ASSERT_NE(wideLiteral, nullptr);
  std::string expectedBits(130, '0');
  expectedBits[0] = '1';
  expectedBits[125] = 'z';
  expectedBits[126] = 'x';
  expectedBits[128] = '1';
  EXPECT_EQ(std::string(wideLiteral), "130'b" + expectedBits);
  uint32_t count = 0;
  ASSERT_EQ(talos_rt_v1_mailbox_num(objects[0], &count), TALOS_RT_OK);
  EXPECT_EQ(count, 2u); // Iteration does not consume FIFO messages.
  std::array<uint8_t, 17> removed{}, removedUnknown{};
  uint32_t present = 0;
  ASSERT_EQ(talos_rt_v1_mailbox_try_get(objects[0], removed.data(),
                                          removedUnknown.data(), &present),
            TALOS_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(removed, firstValue);
  EXPECT_EQ(removedUnknown, firstUnknown);
  checkFirst();
  vpiHandle second = keep(vpi_scan(wideIterator));
  s_vpi_value secondRead{};
  secondRead.format = vpiObjTypeVal;
  vpi_get_value(second, &secondRead);
  EXPECT_EQ(secondRead.format, vpiVectorVal);
  ASSERT_NE(secondRead.value.vector, nullptr);
  EXPECT_EQ(secondRead.value.vector[2].aval, 0x80);
  EXPECT_EQ(secondRead.value.vector[4].aval, 0);
  EXPECT_EQ(vpi_compare_objects(first, second), 0);
  EXPECT_EQ(vpi_scan(wideIterator), nullptr);
  // Retain a partially scanned snapshot iterator to test weak Use lifetime.
  vpiHandle pending = iterator(0);
  vpiHandle remaining = keep(vpi_scan(pending));
  ASSERT_NE(remaining, nullptr);

  vpiHandle realIterator = iterator(1);
  vpiHandle realMessage = keep(vpi_scan(realIterator));
  vpiHandle equalRealMessage = keep(vpi_scan(realIterator));
  EXPECT_EQ(vpi_compare_objects(realMessage, equalRealMessage), 0);
  EXPECT_EQ(vpi_scan(realIterator), nullptr);
  vpiHandle stringMessage = single(2);
  vpiHandle shortRealMessage = single(5);
  vpiHandle wordMessage = single(6);
  const std::array<vpiHandle, 5> scalarMessages{
      first, realMessage, shortRealMessage, stringMessage, wordMessage};
  const std::array<PLI_INT32, 5> scalarKinds{
      vpiLogicTypespec, vpiRealTypespec, vpiShortRealTypespec,
      vpiStringTypespec, vpiIntTypespec};
  std::array<vpiHandle, 5> scalarTypes{};
  for (size_t i = 0; i != scalarMessages.size(); ++i) {
    scalarTypes[i] = keep(vpi_handle(vpiTypespec, scalarMessages[i]));
    vpiHandle again = keep(vpi_handle(vpiTypespec, scalarMessages[i]));
    EXPECT_EQ(vpi_get(vpiType, scalarTypes[i]), scalarKinds[i]);
    EXPECT_EQ(vpi_compare_objects(scalarTypes[i], again), 1);
  }
  vpiHandle innerPacked = keep(vpi_handle(vpiElemTypespec, scalarTypes[0]));
  EXPECT_EQ(vpi_get(vpiType, innerPacked), vpiLogicTypespec);
  vpiHandle logicLeaf = keep(vpi_handle(vpiElemTypespec, innerPacked));
  EXPECT_EQ(vpi_get(vpiType, logicLeaf), vpiLogicTypespec);
  auto checkWord = [&] {
    EXPECT_EQ(vpi_get(vpiSize, wordMessage), 32);
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(wordMessage, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value.value.integer, signedWord);
  };
  checkWord();
  auto checkReal = [&](vpiHandle handle, double expected) {
    EXPECT_EQ(vpi_get(vpiType, handle), vpiConstant);
    EXPECT_EQ(vpi_get(vpiConstType, handle), vpiRealConst);
    s_vpi_value value{};
    value.format = vpiRealVal;
    vpi_get_value(handle, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_DOUBLE_EQ(value.value.real, expected);
  };
  auto checkString = [&] {
    EXPECT_EQ(vpi_get(vpiType, stringMessage), vpiConstant);
    EXPECT_EQ(vpi_get(vpiConstType, stringMessage), vpiStringConst);
    EXPECT_EQ(vpi_get(vpiSize, stringMessage), text.size() * 8);
    s_vpi_value value{};
    value.format = vpiStringVal;
    vpi_get_value(stringMessage, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_NE(value.value.str, nullptr);
    EXPECT_EQ(std::string(value.value.str), text);
  };
  checkReal(realMessage, real);
  checkReal(equalRealMessage, real);
  checkReal(shortRealMessage, shortReal);
  checkString();
  const char *stringLiteral = vpi_get_str(vpiDecompile, stringMessage);
  ASSERT_NE(stringLiteral, nullptr);
  EXPECT_EQ(std::string(stringLiteral),
            "\"snapshot survives mailbox collection\\012\\\"quoted\\\"\"");
  s_vpi_value unsupportedString{};
  unsupportedString.format = vpiIntVal;
  vpi_get_value(stringMessage, &unsupportedString);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);

  vpiHandle classIterator = iterator(3);
  vpiHandle classMessage = vpi_scan(classIterator);
  vpiHandle nullMessage = keep(vpi_scan(classIterator));
  ASSERT_NE(classMessage, nullptr);
  ASSERT_NE(nullMessage, nullptr);
  EXPECT_EQ(vpi_scan(classIterator), nullptr);
  vpiHandle semaphoreIterator = iterator(4);
  vpiHandle semaphoreMessage = vpi_scan(semaphoreIterator);
  ASSERT_NE(semaphoreMessage, nullptr);
  EXPECT_EQ(vpi_scan(semaphoreIterator), nullptr);
  std::array<vpiHandle, 2> referents{};
  for (size_t i = 0; i != referents.size(); ++i) {
    vpiHandle message = i ? semaphoreMessage : classMessage;
    EXPECT_EQ(vpi_get(vpiType, message), vpiClassVar);
    EXPECT_EQ(vpi_get64(vpiObjId, message),
              static_cast<PLI_INT64>(talos_rt_v1_object_id(objects[7 + i])));
    referents[i] = vpi_handle(vpiClassObj, message);
    ASSERT_NE(referents[i], nullptr);
    vpiHandle declared = keep(vpi_handle(vpiClassTypespec, message));
    vpiHandle actual = keep(vpi_handle(vpiClassTypespec, referents[i]));
    EXPECT_EQ(vpi_compare_objects(declared, actual), 1);
    s_vpi_value write{};
    write.format = vpiIntVal;
    write.value.integer = 0;
    EXPECT_EQ(vpi_put_value(message, &write, nullptr, vpiNoDelay), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  }
  EXPECT_EQ(vpi_get(vpiType, nullMessage), vpiClassVar);
  EXPECT_EQ(vpi_get64(vpiObjId, nullMessage), 0);
  EXPECT_EQ(vpi_handle(vpiClassObj, nullMessage), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  // Clearing language roots collects the mailbox, string storage, and class
  // referents. VPI snapshots preserve values, never SystemVerilog liveness.
  for (auto cursor : storage)
    ASSERT_EQ(
        talos_rt_v1_design_write(runtime, cursor, &nullWord, nullptr, 64),
        TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_collect(lane), TALOS_RT_OK);
  talos_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(talos_rt_v1_gc_statistics(runtime, &statistics), TALOS_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  for (vpiHandle mailbox : mailboxes) {
    EXPECT_EQ(vpi_get(vpiType, mailbox), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  }
  EXPECT_EQ(vpi_scan(pending), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  for (vpiHandle handle :
       {classMessage, semaphoreMessage, referents[0], referents[1]}) {
    EXPECT_EQ(vpi_get(vpiType, handle), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiError);
  }
  checkFirst();
  checkReal(realMessage, real);
  checkReal(equalRealMessage, real);
  checkReal(shortRealMessage, shortReal);
  checkString();
  checkWord();
  EXPECT_EQ(vpi_get(vpiType, nullMessage), vpiClassVar);
  EXPECT_EQ(vpi_get64(vpiObjId, nullMessage), 0);
  for (size_t i = 0; i != scalarMessages.size(); ++i) {
    EXPECT_EQ(vpi_get(vpiType, scalarTypes[i]), scalarKinds[i]);
    vpiHandle again = keep(vpi_handle(vpiTypespec, scalarMessages[i]));
    EXPECT_EQ(vpi_compare_objects(scalarTypes[i], again), 1);
  }
  EXPECT_EQ(vpi_get(vpiType, innerPacked), vpiLogicTypespec);
  EXPECT_EQ(vpi_get(vpiType, logicLeaf), vpiLogicTypespec);
  for (vpiHandle handle : persistent)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  ASSERT_EQ(talos_rt_v1_gc_lane_leave(lane), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_gc_lane_destroy(lane), TALOS_RT_OK);
  talos_rt_v1_context_destroy(runtime);
}

} // namespace
