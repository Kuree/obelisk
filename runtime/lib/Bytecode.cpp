//===- Bytecode.cpp - Shared native/bytecode fragment dispatch -----------===//

#include "obelisk/Runtime/Runtime.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

class RegisterFile {
public:
  RegisterFile(uint8_t *data, uint32_t count) : data(data), count(count) {}

  bool contains(uint16_t index) const { return index < count; }
  obelisk_rt_bytecode_type type(uint16_t index) const {
    return data[static_cast<uint64_t>(index) *
                OBELISK_RT_BYTECODE_REGISTER_SIZE];
  }
  uint64_t value(uint16_t index) const {
    uint64_t result;
    std::memcpy(&result,
                data +
                    static_cast<uint64_t>(index) *
                        OBELISK_RT_BYTECODE_REGISTER_SIZE +
                    8,
                sizeof(result));
    return result;
  }
  void define(uint16_t index, obelisk_rt_bytecode_type type, uint64_t value) {
    uint8_t *slot =
        data + static_cast<uint64_t>(index) * OBELISK_RT_BYTECODE_REGISTER_SIZE;
    slot[0] = type;
    std::memcpy(slot + 8, &value, sizeof(value));
  }

private:
  uint8_t *data;
  uint32_t count;
};

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8;
}

uint64_t readU64(const uint8_t *data) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(data[index]) << (index * 8);
  return value;
}

bool isScalarType(obelisk_rt_bytecode_type type) {
  return type == OBELISK_RT_BC_TYPE_U64 || type == OBELISK_RT_BC_TYPE_I64 ||
         type == OBELISK_RT_BC_TYPE_BOOL;
}

bool validSource(uint16_t index, obelisk_rt_bytecode_type type,
                 const RegisterFile &registers) {
  return isScalarType(type) && registers.contains(index) &&
         registers.type(index) == type;
}

bool validFrameRange(uint64_t offset, uint64_t frameSize) {
  return offset <= frameSize && sizeof(uint64_t) <= frameSize - offset;
}

bool validateEntryTable(const obelisk_rt_bytecode_v1 &program,
                        uint64_t instructionCount) {
  uint32_t previousContinuation = 0;
  for (uint32_t index = 0; index != program.entry_count; ++index) {
    const obelisk_rt_bytecode_entry_v1 &entry = program.entries[index];
    if (entry.instruction >= instructionCount ||
        (index != 0 && entry.continuation <= previousContinuation))
      return false;
    previousContinuation = entry.continuation;
  }
  return true;
}

bool ensureEntryTableValidated(const obelisk_rt_bytecode_v1 &program,
                               uint64_t instructionCount) {
  if (!program.validation)
    return validateEntryTable(program, instructionCount);
  enum : uint32_t { Unvalidated, Validating, Valid, Invalid };
  uint32_t state =
      __atomic_load_n(&program.validation->state, __ATOMIC_ACQUIRE);
  if (state == Valid)
    return true;
  if (state == Invalid)
    return false;
  if (state == Unvalidated &&
      __atomic_compare_exchange_n(&program.validation->state, &state,
                                  Validating, false, __ATOMIC_ACQ_REL,
                                  __ATOMIC_ACQUIRE)) {
    bool valid = program.validation->reserved == 0 &&
                 validateEntryTable(program, instructionCount);
    __atomic_store_n(&program.validation->state, valid ? Valid : Invalid,
                     __ATOMIC_RELEASE);
    return valid;
  }
  do {
    state = __atomic_load_n(&program.validation->state, __ATOMIC_ACQUIRE);
  } while (state == Validating);
  return state == Valid;
}

obelisk_rt_status executeBytecode(const obelisk_rt_bytecode_v1 &program,
                                  void *frame, uint64_t frameSize,
                                  uint32_t continuation,
                                  obelisk_rt_fragment_action_v1 *action) {
  if ((program.code_size != 0 && !program.code) || program.code_size == 0 ||
      program.code_size % OBELISK_RT_BYTECODE_INSTRUCTION_SIZE != 0 ||
      program.register_count > static_cast<uint32_t>(UINT16_MAX) + 1u ||
      program.entry_count == 0 || !program.entries ||
      (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_BYTECODE;

  const uint64_t instructionCount =
      program.code_size / OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
  if (!ensureEntryTableValidated(program, instructionCount))
    return OBELISK_RT_INVALID_BYTECODE;
  uint32_t low = 0, high = program.entry_count;
  while (low != high) {
    uint32_t middle = low + (high - low) / 2;
    if (program.entries[middle].continuation < continuation)
      low = middle + 1;
    else
      high = middle;
  }
  if (low == program.entry_count ||
      program.entries[low].continuation != continuation ||
      program.entries[low].instruction >= instructionCount)
    return OBELISK_RT_INVALID_BYTECODE;

  uint64_t registerBytes = static_cast<uint64_t>(program.register_count) *
                           OBELISK_RT_BYTECODE_REGISTER_SIZE;
  if (program.register_offset > frameSize ||
      registerBytes > frameSize - program.register_offset ||
      (registerBytes != 0 && !frame))
    return OBELISK_RT_INVALID_BYTECODE;
  uint8_t *registerData = registerBytes == 0 ? nullptr
                                             : static_cast<uint8_t *>(frame) +
                                                   program.register_offset;
  if (registerBytes != 0)
    std::memset(registerData, 0, registerBytes);
  RegisterFile registers(registerData, program.register_count);

  uint64_t pc = program.entries[low].instruction;
  while (pc < instructionCount) {
    const uint8_t *instruction =
        program.code + pc * OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
    const auto opcode = static_cast<obelisk_rt_bytecode_opcode>(instruction[0]);
    const auto type = static_cast<obelisk_rt_bytecode_type>(instruction[1]);
    const uint16_t destination = readU16(instruction + 2);
    const uint16_t source0 = readU16(instruction + 4);
    const uint16_t source1 = readU16(instruction + 6);
    const uint64_t immediate = readU64(instruction + 8);
    ++pc;

    auto define = [&](uint64_t value,
                      obelisk_rt_bytecode_type resultType) -> bool {
      if (!registers.contains(destination) || !isScalarType(resultType))
        return false;
      registers.define(destination, resultType, value);
      return true;
    };
    auto binary = [&](auto operation,
                      obelisk_rt_bytecode_type resultType) -> bool {
      if (!isScalarType(type) || !validSource(source0, type, registers) ||
          !validSource(source1, type, registers))
        return false;
      return define(
          operation(registers.value(source0), registers.value(source1)),
          resultType);
    };

    switch (opcode) {
    case OBELISK_RT_BC_NOP:
      if (type != OBELISK_RT_BC_TYPE_NONE)
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_CONST:
      if ((type == OBELISK_RT_BC_TYPE_BOOL && immediate > 1) ||
          !define(immediate, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_MOVE:
      if (!validSource(source0, type, registers) ||
          !define(registers.value(source0), type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_ADD:
      if (type == OBELISK_RT_BC_TYPE_BOOL ||
          !binary([](uint64_t lhs, uint64_t rhs) { return lhs + rhs; }, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_SUB:
      if (type == OBELISK_RT_BC_TYPE_BOOL ||
          !binary([](uint64_t lhs, uint64_t rhs) { return lhs - rhs; }, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_MUL:
      if (type == OBELISK_RT_BC_TYPE_BOOL ||
          !binary([](uint64_t lhs, uint64_t rhs) { return lhs * rhs; }, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_AND:
      if (!binary([](uint64_t lhs, uint64_t rhs) { return lhs & rhs; }, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_OR:
      if (!binary([](uint64_t lhs, uint64_t rhs) { return lhs | rhs; }, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_XOR:
      if (!binary([](uint64_t lhs, uint64_t rhs) { return lhs ^ rhs; }, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_NOT:
      if (!validSource(source0, type, registers) ||
          !define(type == OBELISK_RT_BC_TYPE_BOOL ? !registers.value(source0)
                                                  : ~registers.value(source0),
                  type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_EQ:
      if (!binary([](uint64_t lhs, uint64_t rhs) { return lhs == rhs; },
                  OBELISK_RT_BC_TYPE_BOOL))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_ULT:
      if (type != OBELISK_RT_BC_TYPE_U64 ||
          !binary([](uint64_t lhs, uint64_t rhs) { return lhs < rhs; },
                  OBELISK_RT_BC_TYPE_BOOL))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_SLT:
      if (type != OBELISK_RT_BC_TYPE_I64 ||
          !binary(
              [](uint64_t lhs, uint64_t rhs) {
                constexpr uint64_t sign = uint64_t{1} << 63;
                return (lhs ^ sign) < (rhs ^ sign);
              },
              OBELISK_RT_BC_TYPE_BOOL))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_LOAD_FRAME: {
      if (!validFrameRange(immediate, program.register_offset) ||
          !isScalarType(type))
        return OBELISK_RT_INVALID_BYTECODE;
      uint64_t value;
      std::memcpy(&value, static_cast<uint8_t *>(frame) + immediate,
                  sizeof(value));
      if ((type == OBELISK_RT_BC_TYPE_BOOL && value > 1) ||
          !define(value, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    }
    case OBELISK_RT_BC_STORE_FRAME:
      if (!validFrameRange(immediate, program.register_offset) ||
          !validSource(source0, type, registers))
        return OBELISK_RT_INVALID_BYTECODE;
      {
        uint64_t value = registers.value(source0);
        std::memcpy(static_cast<uint8_t *>(frame) + immediate, &value,
                    sizeof(value));
      }
      break;
    case OBELISK_RT_BC_JUMP:
      if (type != OBELISK_RT_BC_TYPE_NONE || immediate >= instructionCount)
        return OBELISK_RT_INVALID_BYTECODE;
      pc = immediate;
      break;
    case OBELISK_RT_BC_BRANCH_ZERO:
      if (!validSource(source0, type, registers) ||
          immediate >= instructionCount)
        return OBELISK_RT_INVALID_BYTECODE;
      if (registers.value(source0) == 0)
        pc = immediate;
      break;
    case OBELISK_RT_BC_CONTINUE:
      if (type != OBELISK_RT_BC_TYPE_NONE || immediate > UINT32_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      *action = {OBELISK_RT_FRAGMENT_CONTINUE,
                 OBELISK_RT_SUSPEND_NONE,
                 static_cast<uint32_t>(immediate),
                 0,
                 0,
                 0};
      return OBELISK_RT_OK;
    case OBELISK_RT_BC_SUSPEND:
      if (type != OBELISK_RT_BC_TYPE_NONE ||
          source0 < OBELISK_RT_SUSPEND_DELAY ||
          source0 > OBELISK_RT_SUSPEND_FRONTIER || immediate > UINT32_MAX ||
          (source1 != UINT16_MAX &&
           !validSource(source1, OBELISK_RT_BC_TYPE_U64, registers)))
        return OBELISK_RT_INVALID_BYTECODE;
      *action = {OBELISK_RT_FRAGMENT_SUSPEND,
                 static_cast<obelisk_rt_suspend_kind>(source0),
                 static_cast<uint32_t>(immediate),
                 0,
                 source1 == UINT16_MAX ? 0 : registers.value(source1),
                 0};
      return OBELISK_RT_OK;
    case OBELISK_RT_BC_TERMINATE:
      if (type != OBELISK_RT_BC_TYPE_NONE)
        return OBELISK_RT_INVALID_BYTECODE;
      *action = {OBELISK_RT_FRAGMENT_TERMINATE,
                 OBELISK_RT_SUSPEND_NONE,
                 0,
                 0,
                 immediate,
                 0};
      return OBELISK_RT_OK;
    default:
      return OBELISK_RT_INVALID_BYTECODE;
    }
  }
  return OBELISK_RT_INVALID_BYTECODE;
}

bool validAction(const obelisk_rt_fragment_action_v1 &action) {
  if (action.flags != OBELISK_RT_FRAGMENT_FLAGS_NONE)
    return false;
  switch (action.kind) {
  case OBELISK_RT_FRAGMENT_CONTINUE:
    return action.suspend_kind == OBELISK_RT_SUSPEND_NONE &&
           action.payload == 0 && action.auxiliary == 0;
  case OBELISK_RT_FRAGMENT_SUSPEND:
    return action.suspend_kind >= OBELISK_RT_SUSPEND_DELAY &&
           action.suspend_kind <= OBELISK_RT_SUSPEND_FRONTIER;
  case OBELISK_RT_FRAGMENT_TERMINATE:
    return action.suspend_kind == OBELISK_RT_SUSPEND_NONE &&
           action.continuation == 0 && action.auxiliary == 0;
  default:
    return false;
  }
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_fragment_execute(
    const obelisk_rt_fragment_descriptor_v1 *descriptor,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint32_t continuation, obelisk_rt_fragment_action_v1 *outAction) {
  if (!descriptor || !outAction ||
      descriptor->handle.kind != OBELISK_RT_DESCRIPTOR_FRAGMENT ||
      (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outAction = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (descriptor->flags != OBELISK_RT_FRAGMENT_FLAGS_NONE)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status status;
  if (descriptor->code_kind == OBELISK_RT_FRAGMENT_NATIVE) {
    if (!descriptor->code.native_entry)
      return OBELISK_RT_INVALID_ARGUMENT;
    try {
      status = descriptor->code.native_entry(context, frame, frameSize,
                                             continuation, outAction);
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (...) {
      return OBELISK_RT_INVALID_ARGUMENT;
    }
  } else if (descriptor->code_kind == OBELISK_RT_FRAGMENT_BYTECODE) {
    status = executeBytecode(descriptor->code.bytecode, frame, frameSize,
                             continuation, outAction);
  } else {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  if (status != OBELISK_RT_OK)
    return status;
  return validAction(*outAction) ? OBELISK_RT_OK : OBELISK_RT_INVALID_ARGUMENT;
}
