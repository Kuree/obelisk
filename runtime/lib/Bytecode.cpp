//===- Bytecode.cpp - Shared native/bytecode fragment dispatch -----------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <optional>
#include <vector>

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
  void clear(uint16_t index) {
    uint8_t *slot =
        data + static_cast<uint64_t>(index) * OBELISK_RT_BYTECODE_REGISTER_SIZE;
    std::memset(slot, 0, OBELISK_RT_BYTECODE_REGISTER_SIZE);
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

bool validRange(uint64_t offset, uint64_t size, uint64_t limit) {
  return offset <= limit && size <= limit - offset;
}

uint64_t scalarByteSize(obelisk_rt_bytecode_value_kind kind) {
  switch (kind) {
  case OBELISK_RT_BC_VALUE_U8:
    return 1;
  case OBELISK_RT_BC_VALUE_U32:
  case OBELISK_RT_BC_VALUE_I32:
    return 4;
  case OBELISK_RT_BC_VALUE_U64:
  case OBELISK_RT_BC_VALUE_I64:
    return 8;
  default:
    return 0;
  }
}

bool validOperandRange(const obelisk_rt_bytecode_operand_v1 &operand,
                       const obelisk_rt_bytecode_v1 &program,
                       uint64_t byteSize) {
  if (operand.kind == OBELISK_RT_BC_OPERAND_FRAME)
    return validRange(operand.value, byteSize, program.register_offset);
  if (operand.kind == OBELISK_RT_BC_OPERAND_CONSTANT)
    return validRange(operand.value, byteSize, program.constant_size);
  return false;
}

bool validArgumentOperand(const obelisk_rt_bytecode_operand_v1 &operand,
                          const obelisk_rt_bytecode_v1 &program) {
  if (operand.reserved != 0 ||
      operand.direction != OBELISK_RT_BC_OPERAND_INPUT ||
      (operand.flags &
       ~(OBELISK_RT_ARG_SIGNED | OBELISK_RT_ARG_FORMAT_STRING)) != 0)
    return false;
  switch (operand.value_kind) {
  case OBELISK_RT_BC_VALUE_ARGUMENT_EMPTY:
    return operand.kind == OBELISK_RT_BC_OPERAND_IMMEDIATE &&
           operand.flags == 0 && operand.value == 0 && operand.size == 0 &&
           operand.auxiliary == 0;
  case OBELISK_RT_BC_VALUE_ARGUMENT_STRING:
    return (operand.flags & OBELISK_RT_ARG_SIGNED) == 0 &&
           (operand.kind == OBELISK_RT_BC_OPERAND_FRAME ||
            operand.kind == OBELISK_RT_BC_OPERAND_CONSTANT) &&
           operand.auxiliary == 0 &&
           validOperandRange(operand, program, operand.size);
  case OBELISK_RT_BC_VALUE_ARGUMENT_REAL:
  case OBELISK_RT_BC_VALUE_ARGUMENT_TIME:
    return operand.flags == 0 && operand.size == 8 && operand.auxiliary == 0 &&
           validOperandRange(operand, program, sizeof(uint64_t));
  case OBELISK_RT_BC_VALUE_ARGUMENT_LOGIC: {
    if (operand.size == 0 ||
        (operand.flags & OBELISK_RT_ARG_FORMAT_STRING) != 0 ||
        (operand.kind != OBELISK_RT_BC_OPERAND_FRAME &&
         operand.kind != OBELISK_RT_BC_OPERAND_CONSTANT))
      return false;
    uint64_t words = operand.size / 64 + (operand.size % 64 != 0);
    if (words > std::numeric_limits<uint64_t>::max() / sizeof(uint64_t))
      return false;
    uint64_t bytes = words * sizeof(uint64_t);
    if (!validOperandRange(operand, program, bytes))
      return false;
    if (operand.auxiliary == UINT64_MAX)
      return true;
    uint64_t limit = operand.kind == OBELISK_RT_BC_OPERAND_FRAME
                         ? program.register_offset
                         : program.constant_size;
    return validRange(operand.auxiliary, bytes, limit);
  }
  default:
    return false;
  }
}

bool validServiceOperand(const obelisk_rt_bytecode_operand_v1 &operand,
                         const obelisk_rt_bytecode_v1 &program,
                         obelisk_rt_bytecode_value_kind expectedKind,
                         obelisk_rt_bytecode_operand_direction direction) {
  if (operand.reserved != 0 || operand.flags != 0 ||
      operand.direction != direction || operand.value_kind != expectedKind)
    return false;
  uint64_t scalarSize = scalarByteSize(expectedKind);
  if (scalarSize != 0) {
    if (operand.auxiliary != 0)
      return false;
    if (direction == OBELISK_RT_BC_OPERAND_OUTPUT) {
      if (operand.kind == OBELISK_RT_BC_OPERAND_REGISTER)
        return operand.value < program.register_count && operand.size == 0;
      return operand.kind == OBELISK_RT_BC_OPERAND_FRAME &&
             operand.size == scalarSize &&
             validOperandRange(operand, program, scalarSize);
    }
    if (operand.kind == OBELISK_RT_BC_OPERAND_IMMEDIATE)
      return operand.size == 0 &&
             (scalarSize == 8 ||
              operand.value < (uint64_t{1} << (scalarSize * 8)));
    if (operand.kind == OBELISK_RT_BC_OPERAND_REGISTER)
      return operand.value < program.register_count && operand.size == 0;
    return operand.size == scalarSize &&
           validOperandRange(operand, program, scalarSize);
  }
  switch (expectedKind) {
  case OBELISK_RT_BC_VALUE_BYTES:
    if (direction != OBELISK_RT_BC_OPERAND_INPUT || operand.auxiliary != 0)
      return false;
    if (operand.kind == OBELISK_RT_BC_OPERAND_RESOURCE)
      return operand.value < program.register_count && operand.size == 0;
    return (operand.kind == OBELISK_RT_BC_OPERAND_FRAME ||
            operand.kind == OBELISK_RT_BC_OPERAND_CONSTANT) &&
           validOperandRange(operand, program, operand.size);
  case OBELISK_RT_BC_VALUE_MUTABLE_BYTES:
    return direction == OBELISK_RT_BC_OPERAND_INOUT &&
           operand.kind == OBELISK_RT_BC_OPERAND_FRAME &&
           operand.auxiliary == 0 &&
           validOperandRange(operand, program, operand.size);
  case OBELISK_RT_BC_VALUE_BUFFER:
    if (direction == OBELISK_RT_BC_OPERAND_OUTPUT)
      return operand.kind == OBELISK_RT_BC_OPERAND_REGISTER &&
             operand.value < program.register_count && operand.size == 0 &&
             operand.auxiliary == 0;
    return direction == OBELISK_RT_BC_OPERAND_INPUT &&
           operand.kind == OBELISK_RT_BC_OPERAND_RESOURCE &&
           operand.value < program.register_count && operand.size == 0 &&
           operand.auxiliary == 0;
  case OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY: {
    if (direction != OBELISK_RT_BC_OPERAND_INPUT ||
        operand.kind != OBELISK_RT_BC_OPERAND_IMMEDIATE ||
        operand.auxiliary != 0 ||
        !validRange(operand.value, operand.size, program.operand_count))
      return false;
    for (uint64_t index = 0; index != operand.size; ++index)
      if (!validArgumentOperand(program.operands[operand.value + index],
                                program))
        return false;
    return true;
  }
  case OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT: {
    if (direction != OBELISK_RT_BC_OPERAND_INPUT ||
        operand.kind != OBELISK_RT_BC_OPERAND_IMMEDIATE ||
        operand.auxiliary != 0)
      return false;
    if (operand.size == 0)
      return operand.value == 0;
    if (operand.size != 5 ||
        !validRange(operand.value, operand.size, program.operand_count))
      return false;
    const auto *children = program.operands + operand.value;
    return validServiceOperand(children[0], program, OBELISK_RT_BC_VALUE_BYTES,
                               OBELISK_RT_BC_OPERAND_INPUT) &&
           validServiceOperand(children[1], program, OBELISK_RT_BC_VALUE_BYTES,
                               OBELISK_RT_BC_OPERAND_INPUT) &&
           validServiceOperand(children[2], program, OBELISK_RT_BC_VALUE_U32,
                               OBELISK_RT_BC_OPERAND_INPUT) &&
           validServiceOperand(children[3], program, OBELISK_RT_BC_VALUE_BYTES,
                               OBELISK_RT_BC_OPERAND_INPUT) &&
           validServiceOperand(children[4], program, OBELISK_RT_BC_VALUE_U64,
                               OBELISK_RT_BC_OPERAND_INPUT);
  }
  default:
    return false;
  }
}

bool validateServiceSite(const obelisk_rt_bytecode_service_site_v1 &site,
                         const obelisk_rt_bytecode_v1 &program) {
  if (site.flags != 0 || site.reserved != 0 ||
      !validRange(site.first_operand, site.operand_count,
                  program.operand_count))
    return false;
  const auto *operands =
      site.operand_count == 0 ? nullptr : program.operands + site.first_operand;
  auto matches =
      [&](std::initializer_list<obelisk_rt_bytecode_value_kind> kinds,
          size_t outputCount = 0,
          std::optional<size_t> inoutIndex = std::nullopt) {
        if (kinds.size() != site.operand_count || outputCount > kinds.size())
          return false;
        size_t index = 0;
        for (auto kind : kinds) {
          auto direction = inoutIndex && index == *inoutIndex
                               ? OBELISK_RT_BC_OPERAND_INOUT
                           : index >= kinds.size() - outputCount
                               ? OBELISK_RT_BC_OPERAND_OUTPUT
                               : OBELISK_RT_BC_OPERAND_INPUT;
          ++index;
          if (!validServiceOperand(operands[index - 1], program, kind,
                                   direction))
            return false;
        }
        return true;
      };
  bool valid = false;
  switch (site.service) {
  case OBELISK_RT_BC_SERVICE_FORMAT:
    valid = matches(
        {OBELISK_RT_BC_VALUE_BYTES, OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY,
         OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT, OBELISK_RT_BC_VALUE_BUFFER},
        1);
    break;
  case OBELISK_RT_BC_SERVICE_DISPLAY:
    valid =
        matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_U32,
                 OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_ARGUMENT_ARRAY,
                 OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT});
    break;
  case OBELISK_RT_BC_SERVICE_BUFFER_RELEASE:
    valid = matches({OBELISK_RT_BC_VALUE_BUFFER});
    break;
  case OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD:
    valid = matches({OBELISK_RT_BC_VALUE_BYTES, OBELISK_RT_BC_VALUE_U32}, 1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_OPEN:
    valid = matches({OBELISK_RT_BC_VALUE_BYTES, OBELISK_RT_BC_VALUE_BYTES,
                     OBELISK_RT_BC_VALUE_U32},
                    1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_CLOSE:
  case OBELISK_RT_BC_SERVICE_FILE_FLUSH:
  case OBELISK_RT_BC_SERVICE_FILE_REWIND:
    valid = matches({OBELISK_RT_BC_VALUE_U32});
    break;
  case OBELISK_RT_BC_SERVICE_FILE_WRITE:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_BYTES,
                     OBELISK_RT_BC_VALUE_U64},
                    1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_READ:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_MUTABLE_BYTES,
                     OBELISK_RT_BC_VALUE_U64},
                    1, 1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_GETC:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_U8}, 1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_UNGETC:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_U8});
    break;
  case OBELISK_RT_BC_SERVICE_FILE_GETLINE:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_U64,
                     OBELISK_RT_BC_VALUE_BUFFER},
                    1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_EOF:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_U32}, 1);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_ERROR:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_I32,
                     OBELISK_RT_BC_VALUE_BUFFER},
                    2);
    break;
  case OBELISK_RT_BC_SERVICE_FILE_SEEK:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_I64,
                     OBELISK_RT_BC_VALUE_U32});
    break;
  case OBELISK_RT_BC_SERVICE_FILE_TELL:
    valid = matches({OBELISK_RT_BC_VALUE_U32, OBELISK_RT_BC_VALUE_I64}, 1);
    break;
  default:
    return false;
  }
  if (!valid)
    return false;

  // Runtime out-parameters and mutable spans are distinct compiler-generated
  // storage. Reject ambiguous aliasing rather than inheriting host C aliasing
  // accidents in the bytecode encoding.
  for (uint32_t index = 0; index != site.operand_count; ++index) {
    const auto &lhs = operands[index];
    if (lhs.direction == OBELISK_RT_BC_OPERAND_INPUT ||
        lhs.kind != OBELISK_RT_BC_OPERAND_FRAME || lhs.size == 0)
      continue;
    for (uint32_t otherIndex = 0; otherIndex != index; ++otherIndex) {
      const auto &rhs = operands[otherIndex];
      if (rhs.kind != OBELISK_RT_BC_OPERAND_FRAME || rhs.size == 0)
        continue;
      if (lhs.value < rhs.value + rhs.size && rhs.value < lhs.value + lhs.size)
        return false;
    }
  }

  // Service results are committed independently. Reusing an output register
  // would make a later result overwrite an earlier resource or scalar.
  for (uint32_t index = 0; index != site.operand_count; ++index) {
    const auto &output = operands[index];
    if (output.direction != OBELISK_RT_BC_OPERAND_OUTPUT ||
        output.kind != OBELISK_RT_BC_OPERAND_REGISTER)
      continue;
    for (uint32_t previous = 0; previous != index; ++previous) {
      const auto &other = operands[previous];
      if (other.direction == OBELISK_RT_BC_OPERAND_OUTPUT &&
          other.kind == OBELISK_RT_BC_OPERAND_REGISTER &&
          other.value == output.value)
        return false;
    }
  }
  return true;
}

bool validateInstructionEncoding(const obelisk_rt_bytecode_v1 &program,
                                 uint64_t instructionCount) {
  auto validRegister = [&](uint16_t index) {
    return index < program.register_count;
  };
  for (uint64_t pc = 0; pc != instructionCount; ++pc) {
    const uint8_t *instruction =
        program.code + pc * OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
    auto opcode = static_cast<obelisk_rt_bytecode_opcode>(instruction[0]);
    auto type = static_cast<obelisk_rt_bytecode_type>(instruction[1]);
    uint16_t destination = readU16(instruction + 2);
    uint16_t source0 = readU16(instruction + 4);
    uint16_t source1 = readU16(instruction + 6);
    uint64_t immediate = readU64(instruction + 8);
    auto noSources = [&] { return source0 == 0 && source1 == 0; };
    auto scalarOrStatus = [&] {
      return isScalarType(type) || type == OBELISK_RT_BC_TYPE_STATUS;
    };
    switch (opcode) {
    case OBELISK_RT_BC_NOP:
      if (type != OBELISK_RT_BC_TYPE_NONE || destination != 0 || !noSources() ||
          immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_CONST:
      if (!scalarOrStatus() || !validRegister(destination) || !noSources() ||
          (type == OBELISK_RT_BC_TYPE_BOOL && immediate > 1) ||
          (type == OBELISK_RT_BC_TYPE_STATUS &&
           immediate > OBELISK_RT_STEP_LIMIT))
        return false;
      break;
    case OBELISK_RT_BC_MOVE:
      if (!scalarOrStatus() || !validRegister(destination) ||
          !validRegister(source0) || source1 != 0 || immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_ADD:
    case OBELISK_RT_BC_SUB:
    case OBELISK_RT_BC_MUL:
      if ((type != OBELISK_RT_BC_TYPE_U64 && type != OBELISK_RT_BC_TYPE_I64) ||
          !validRegister(destination) || !validRegister(source0) ||
          !validRegister(source1) || immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_AND:
    case OBELISK_RT_BC_OR:
    case OBELISK_RT_BC_XOR:
      if (!isScalarType(type) || !validRegister(destination) ||
          !validRegister(source0) || !validRegister(source1) || immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_NOT:
      if (!isScalarType(type) || !validRegister(destination) ||
          !validRegister(source0) || source1 != 0 || immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_EQ:
      if (!scalarOrStatus() || !validRegister(destination) ||
          !validRegister(source0) || !validRegister(source1) || immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_ULT:
    case OBELISK_RT_BC_SLT:
      if (type != (opcode == OBELISK_RT_BC_ULT ? OBELISK_RT_BC_TYPE_U64
                                               : OBELISK_RT_BC_TYPE_I64) ||
          !validRegister(destination) || !validRegister(source0) ||
          !validRegister(source1) || immediate != 0)
        return false;
      break;
    case OBELISK_RT_BC_LOAD_FRAME:
      if (!isScalarType(type) || !validRegister(destination) || !noSources() ||
          !validFrameRange(immediate, program.register_offset))
        return false;
      break;
    case OBELISK_RT_BC_STORE_FRAME:
      if (!isScalarType(type) || destination != 0 || !validRegister(source0) ||
          source1 != 0 || !validFrameRange(immediate, program.register_offset))
        return false;
      break;
    case OBELISK_RT_BC_JUMP:
      if (type != OBELISK_RT_BC_TYPE_NONE || destination != 0 || !noSources() ||
          immediate >= instructionCount)
        return false;
      break;
    case OBELISK_RT_BC_BRANCH_ZERO:
      if (!scalarOrStatus() || destination != 0 || !validRegister(source0) ||
          source1 != 0 || immediate >= instructionCount)
        return false;
      break;
    case OBELISK_RT_BC_CONTINUE:
      if (type != OBELISK_RT_BC_TYPE_NONE || destination != 0 || !noSources() ||
          immediate > UINT32_MAX)
        return false;
      break;
    case OBELISK_RT_BC_SUSPEND:
      if (type != OBELISK_RT_BC_TYPE_NONE || destination != 0 ||
          source0 < OBELISK_RT_SUSPEND_DELAY ||
          source0 > OBELISK_RT_SUSPEND_CHILDREN || immediate > UINT32_MAX ||
          (source1 != UINT16_MAX && !validRegister(source1)))
        return false;
      break;
    case OBELISK_RT_BC_TERMINATE:
      if (type != OBELISK_RT_BC_TYPE_NONE || destination != 0 || !noSources())
        return false;
      break;
    case OBELISK_RT_BC_CALL_SERVICE:
      if (type != OBELISK_RT_BC_TYPE_STATUS || !validRegister(destination) ||
          source0 != 0 || source1 != 0 ||
          immediate >= program.service_site_count)
        return false;
      break;
    case OBELISK_RT_BC_FAIL:
      if (type != OBELISK_RT_BC_TYPE_STATUS || destination != 0 ||
          !validRegister(source0) || source1 != 0 || immediate != 0)
        return false;
      break;
    default:
      return false;
    }
  }
  return true;
}

bool validateServiceMetadata(const obelisk_rt_bytecode_v1 &program,
                             uint64_t instructionCount) {
  if (program.reserved != 0 ||
      (program.constant_size != 0 && !program.constants) ||
      (program.service_site_count != 0 && !program.service_sites) ||
      (program.operand_count != 0 && !program.operands))
    return false;
  for (uint64_t index = 0; index != program.operand_count; ++index) {
    const auto &operand = program.operands[index];
    if (operand.reserved != 0 ||
        operand.kind > OBELISK_RT_BC_OPERAND_RESOURCE ||
        operand.direction > OBELISK_RT_BC_OPERAND_INOUT ||
        operand.value_kind > OBELISK_RT_BC_VALUE_ARGUMENT_TIME)
      return false;
    if (operand.value_kind >= OBELISK_RT_BC_VALUE_ARGUMENT_EMPTY) {
      if (!validArgumentOperand(operand, program))
        return false;
    } else if (operand.flags != 0) {
      return false;
    }
  }
  for (uint32_t index = 0; index != program.service_site_count; ++index)
    if (!validateServiceSite(program.service_sites[index], program))
      return false;
  if (!validateInstructionEncoding(program, instructionCount))
    return false;
  for (uint64_t pc = 0; pc != instructionCount; ++pc) {
    const uint8_t *instruction =
        program.code + pc * OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
    auto opcode = static_cast<obelisk_rt_bytecode_opcode>(instruction[0]);
    uint16_t destination = readU16(instruction + 2);
    uint64_t immediate = readU64(instruction + 8);
    if (opcode == OBELISK_RT_BC_CALL_SERVICE) {
      const auto &site = program.service_sites[immediate];
      const auto *operands = program.operands + site.first_operand;
      for (uint32_t index = 0; index != site.operand_count; ++index)
        if (operands[index].direction == OBELISK_RT_BC_OPERAND_OUTPUT &&
            operands[index].kind == OBELISK_RT_BC_OPERAND_REGISTER &&
            operands[index].value == destination)
          return false;
    }
  }
  return true;
}

obelisk_rt_bytecode_type
registerTypeForValue(obelisk_rt_bytecode_value_kind kind);

using AbstractRegisterState = std::vector<uint8_t>;

uint8_t typeMask(obelisk_rt_bytecode_type type) {
  return static_cast<uint8_t>(uint8_t{1} << type);
}

bool requiresType(const AbstractRegisterState &state, uint64_t index,
                  obelisk_rt_bytecode_type type) {
  return index < state.size() && state[index] == typeMask(type);
}

bool validateServiceInputRegisters(
    const obelisk_rt_bytecode_operand_v1 &operand,
    const obelisk_rt_bytecode_v1 &program, const AbstractRegisterState &state) {
  if (operand.direction == OBELISK_RT_BC_OPERAND_OUTPUT)
    return true;
  if (scalarByteSize(operand.value_kind) != 0 &&
      operand.kind == OBELISK_RT_BC_OPERAND_REGISTER)
    return requiresType(state, operand.value,
                        registerTypeForValue(operand.value_kind));
  if ((operand.value_kind == OBELISK_RT_BC_VALUE_BYTES ||
       operand.value_kind == OBELISK_RT_BC_VALUE_BUFFER) &&
      operand.kind == OBELISK_RT_BC_OPERAND_RESOURCE)
    return requiresType(state, operand.value, OBELISK_RT_BC_TYPE_RESOURCE);
  if (operand.value_kind == OBELISK_RT_BC_VALUE_FORMAT_ENVIRONMENT &&
      operand.size != 0) {
    const auto *children = program.operands + operand.value;
    for (uint64_t index = 0; index != operand.size; ++index)
      if (!validateServiceInputRegisters(children[index], program, state))
        return false;
  }
  return true;
}

bool transferServiceRegisters(const obelisk_rt_bytecode_service_site_v1 &site,
                              const obelisk_rt_bytecode_v1 &program,
                              uint16_t statusRegister,
                              AbstractRegisterState &state) {
  const auto *operands = program.operands + site.first_operand;
  // CALL_SERVICE checks every destination before invoking the runtime call.
  // Mirror that check here so validation cannot approve a program whose first
  // execution would discover a forged resource only after side effects.
  if (state[statusRegister] & typeMask(OBELISK_RT_BC_TYPE_RESOURCE))
    return false;
  for (uint32_t index = 0; index != site.operand_count; ++index) {
    const auto &operand = operands[index];
    if (!validateServiceInputRegisters(operand, program, state))
      return false;
    if (operand.direction == OBELISK_RT_BC_OPERAND_OUTPUT &&
        operand.kind == OBELISK_RT_BC_OPERAND_REGISTER &&
        (state[operand.value] & typeMask(OBELISK_RT_BC_TYPE_RESOURCE)))
      return false;
  }

  if (site.service == OBELISK_RT_BC_SERVICE_BUFFER_RELEASE) {
    const auto &resource = operands[0];
    state[resource.value] = typeMask(OBELISK_RT_BC_TYPE_NONE);
  }
  for (uint32_t index = 0; index != site.operand_count; ++index) {
    const auto &operand = operands[index];
    if (operand.direction != OBELISK_RT_BC_OPERAND_OUTPUT ||
        operand.kind != OBELISK_RT_BC_OPERAND_REGISTER)
      continue;
    state[operand.value] = typeMask(registerTypeForValue(operand.value_kind));
  }
  state[statusRegister] = typeMask(OBELISK_RT_BC_TYPE_STATUS);
  return true;
}

bool validateInstructionTypes(const obelisk_rt_bytecode_v1 &program,
                              uint64_t instructionCount) {
  std::vector<AbstractRegisterState> incoming(instructionCount);
  std::vector<bool> reachable(instructionCount, false);
  std::deque<uint64_t> worklist;
  AbstractRegisterState initial(program.register_count,
                                typeMask(OBELISK_RT_BC_TYPE_NONE));
  auto enqueue = [&](uint64_t pc, const AbstractRegisterState &state) {
    if (!reachable[pc]) {
      reachable[pc] = true;
      incoming[pc] = state;
      worklist.push_back(pc);
      return;
    }
    bool changed = false;
    for (uint32_t index = 0; index != program.register_count; ++index) {
      uint8_t merged = incoming[pc][index] | state[index];
      changed |= merged != incoming[pc][index];
      incoming[pc][index] = merged;
    }
    if (changed)
      worklist.push_back(pc);
  };
  for (uint32_t index = 0; index != program.entry_count; ++index)
    enqueue(program.entries[index].instruction, initial);

  while (!worklist.empty()) {
    uint64_t pc = worklist.front();
    worklist.pop_front();
    AbstractRegisterState state = incoming[pc];
    const uint8_t *instruction =
        program.code + pc * OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
    auto opcode = static_cast<obelisk_rt_bytecode_opcode>(instruction[0]);
    auto type = static_cast<obelisk_rt_bytecode_type>(instruction[1]);
    uint16_t destination = readU16(instruction + 2);
    uint16_t source0 = readU16(instruction + 4);
    uint16_t source1 = readU16(instruction + 6);
    uint64_t immediate = readU64(instruction + 8);
    auto require = [&](uint16_t index, obelisk_rt_bytecode_type expected) {
      return requiresType(state, index, expected);
    };
    auto define = [&](obelisk_rt_bytecode_type resultType) {
      if (state[destination] & typeMask(OBELISK_RT_BC_TYPE_RESOURCE))
        return false;
      state[destination] = typeMask(resultType);
      return true;
    };
    bool terminal = false;
    bool jumpOnly = false;
    switch (opcode) {
    case OBELISK_RT_BC_NOP:
      break;
    case OBELISK_RT_BC_CONST:
      if (!define(type))
        return false;
      break;
    case OBELISK_RT_BC_MOVE:
      if (!require(source0, type) || !define(type))
        return false;
      break;
    case OBELISK_RT_BC_ADD:
    case OBELISK_RT_BC_SUB:
    case OBELISK_RT_BC_MUL:
    case OBELISK_RT_BC_AND:
    case OBELISK_RT_BC_OR:
    case OBELISK_RT_BC_XOR:
      if (!require(source0, type) || !require(source1, type) || !define(type))
        return false;
      break;
    case OBELISK_RT_BC_NOT:
      if (!require(source0, type) || !define(type))
        return false;
      break;
    case OBELISK_RT_BC_EQ:
      if (!require(source0, type) || !require(source1, type) ||
          !define(OBELISK_RT_BC_TYPE_BOOL))
        return false;
      break;
    case OBELISK_RT_BC_ULT:
    case OBELISK_RT_BC_SLT:
      if (!require(source0, type) || !require(source1, type) ||
          !define(OBELISK_RT_BC_TYPE_BOOL))
        return false;
      break;
    case OBELISK_RT_BC_LOAD_FRAME:
      if (!define(type))
        return false;
      break;
    case OBELISK_RT_BC_STORE_FRAME:
      if (!require(source0, type))
        return false;
      break;
    case OBELISK_RT_BC_JUMP:
      jumpOnly = true;
      break;
    case OBELISK_RT_BC_BRANCH_ZERO:
      if (!require(source0, type))
        return false;
      enqueue(immediate, state);
      break;
    case OBELISK_RT_BC_CONTINUE:
    case OBELISK_RT_BC_TERMINATE:
      for (uint8_t registerTypes : state)
        if (registerTypes & typeMask(OBELISK_RT_BC_TYPE_RESOURCE))
          return false;
      terminal = true;
      break;
    case OBELISK_RT_BC_SUSPEND:
      if (source1 != UINT16_MAX && !require(source1, OBELISK_RT_BC_TYPE_U64))
        return false;
      for (uint8_t registerTypes : state)
        if (registerTypes & typeMask(OBELISK_RT_BC_TYPE_RESOURCE))
          return false;
      terminal = true;
      break;
    case OBELISK_RT_BC_CALL_SERVICE:
      if (!transferServiceRegisters(program.service_sites[immediate], program,
                                    destination, state))
        return false;
      break;
    case OBELISK_RT_BC_FAIL:
      if (!require(source0, OBELISK_RT_BC_TYPE_STATUS))
        return false;
      terminal = true;
      break;
    default:
      return false;
    }
    if (terminal)
      continue;
    if (jumpOnly) {
      enqueue(immediate, state);
      continue;
    }
    if (pc + 1 >= instructionCount)
      return false;
    enqueue(pc + 1, state);
  }

  return true;
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
  // The validation record is ABI-visible and therefore caller-controlled. It
  // records the latest result but is never trusted to bypass validation.
  bool valid = (!program.validation || program.validation->reserved == 0) &&
               validateEntryTable(program, instructionCount) &&
               validateServiceMetadata(program, instructionCount) &&
               validateInstructionTypes(program, instructionCount);
  if (program.validation)
    __atomic_store_n(&program.validation->state,
                     valid ? OBELISK_RT_BC_VALIDATION_VALID
                           : OBELISK_RT_BC_VALIDATION_INVALID,
                     __ATOMIC_RELEASE);
  return valid;
}

class ResourceTable {
public:
  explicit ResourceTable(uint32_t capacity) : entries(capacity) {}
  ResourceTable(const ResourceTable &) = delete;
  ResourceTable &operator=(const ResourceTable &) = delete;
  ~ResourceTable() { releaseAll(); }

  uint64_t adopt(obelisk_rt_buffer_v1 buffer) {
    for (size_t index = 0; index != entries.size(); ++index)
      if (!entries[index].live) {
        entries[index] = {buffer, true};
        return index + 1;
      }
    return 0;
  }

  obelisk_rt_buffer_v1 *get(uint64_t id) {
    if (id == 0 || id > entries.size() || !entries[id - 1].live)
      return nullptr;
    return &entries[id - 1].buffer;
  }

  bool release(uint64_t id) {
    obelisk_rt_buffer_v1 *buffer = get(id);
    if (!buffer)
      return false;
    obelisk_rt_v1_buffer_release(buffer);
    entries[id - 1].live = false;
    return true;
  }

  bool hasLiveResources() const {
    for (const Entry &entry : entries)
      if (entry.live)
        return true;
    return false;
  }

private:
  struct Entry {
    obelisk_rt_buffer_v1 buffer{};
    bool live = false;
  };

  void releaseAll() {
    for (Entry &entry : entries)
      if (entry.live) {
        obelisk_rt_v1_buffer_release(&entry.buffer);
        entry.live = false;
      }
  }

  std::vector<Entry> entries;
};

obelisk_rt_bytecode_type
registerTypeForValue(obelisk_rt_bytecode_value_kind kind) {
  switch (kind) {
  case OBELISK_RT_BC_VALUE_I32:
  case OBELISK_RT_BC_VALUE_I64:
    return OBELISK_RT_BC_TYPE_I64;
  case OBELISK_RT_BC_VALUE_U8:
  case OBELISK_RT_BC_VALUE_U32:
  case OBELISK_RT_BC_VALUE_U64:
    return OBELISK_RT_BC_TYPE_U64;
  case OBELISK_RT_BC_VALUE_BUFFER:
    return OBELISK_RT_BC_TYPE_RESOURCE;
  default:
    return OBELISK_RT_BC_TYPE_NONE;
  }
}

const uint8_t *operandAddress(const obelisk_rt_bytecode_operand_v1 &operand,
                              const obelisk_rt_bytecode_v1 &program,
                              const void *frame) {
  if (operand.kind == OBELISK_RT_BC_OPERAND_FRAME)
    return frame ? static_cast<const uint8_t *>(frame) + operand.value
                 : nullptr;
  if (operand.kind == OBELISK_RT_BC_OPERAND_CONSTANT)
    return program.constants ? program.constants + operand.value : nullptr;
  return nullptr;
}

std::optional<uint64_t>
readScalarOperand(const obelisk_rt_bytecode_operand_v1 &operand,
                  const obelisk_rt_bytecode_v1 &program, const void *frame,
                  const RegisterFile &registers) {
  uint64_t byteSize = scalarByteSize(operand.value_kind);
  if (byteSize == 0)
    return std::nullopt;
  uint64_t value = 0;
  if (operand.kind == OBELISK_RT_BC_OPERAND_IMMEDIATE)
    value = operand.value;
  else if (operand.kind == OBELISK_RT_BC_OPERAND_REGISTER) {
    auto expected = registerTypeForValue(operand.value_kind);
    if (!registers.contains(operand.value) ||
        registers.type(operand.value) != expected)
      return std::nullopt;
    value = registers.value(operand.value);
  } else {
    const uint8_t *source = operandAddress(operand, program, frame);
    if (!source)
      return std::nullopt;
    for (uint64_t index = 0; index != byteSize; ++index)
      value |= static_cast<uint64_t>(source[index]) << (index * 8);
  }
  if (byteSize < 8) {
    uint64_t mask = (uint64_t{1} << (byteSize * 8)) - 1;
    value &= mask;
    if ((operand.value_kind == OBELISK_RT_BC_VALUE_I32) &&
        (value & (uint64_t{1} << 31)))
      value |= ~UINT64_C(0xffffffff);
  }
  return value;
}

bool writeScalarOperand(const obelisk_rt_bytecode_operand_v1 &operand,
                        void *frame, RegisterFile &registers, uint64_t value) {
  uint64_t byteSize = scalarByteSize(operand.value_kind);
  if (byteSize == 0)
    return false;
  if (operand.kind == OBELISK_RT_BC_OPERAND_REGISTER) {
    uint16_t index = static_cast<uint16_t>(operand.value);
    if (registers.type(index) == OBELISK_RT_BC_TYPE_RESOURCE)
      return false;
    registers.define(index, registerTypeForValue(operand.value_kind), value);
    return true;
  }
  if (operand.kind != OBELISK_RT_BC_OPERAND_FRAME)
    return false;
  uint8_t *destination = static_cast<uint8_t *>(frame) + operand.value;
  for (uint64_t index = 0; index != byteSize; ++index)
    destination[index] = static_cast<uint8_t>(value >> (index * 8));
  return true;
}

struct ByteView {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
};

std::optional<ByteView>
readBytesOperand(const obelisk_rt_bytecode_operand_v1 &operand,
                 const obelisk_rt_bytecode_v1 &program, const void *frame,
                 const RegisterFile &registers, ResourceTable &resources) {
  if (operand.kind == OBELISK_RT_BC_OPERAND_RESOURCE) {
    uint16_t index = static_cast<uint16_t>(operand.value);
    if (registers.type(index) != OBELISK_RT_BC_TYPE_RESOURCE)
      return std::nullopt;
    obelisk_rt_buffer_v1 *buffer = resources.get(registers.value(index));
    if (!buffer)
      return std::nullopt;
    return ByteView{buffer->data, buffer->size};
  }
  const uint8_t *data = operandAddress(operand, program, frame);
  if (!data && operand.size != 0)
    return std::nullopt;
  return ByteView{data, operand.size};
}

struct ArgumentStorage {
  std::vector<obelisk_rt_arg_v1> arguments;
  std::vector<std::vector<uint64_t>> values;
  std::vector<std::vector<uint64_t>> unknowns;
  std::vector<double> reals;
  std::vector<uint64_t> times;
};

std::optional<ArgumentStorage>
buildArguments(const obelisk_rt_bytecode_operand_v1 &array,
               const obelisk_rt_bytecode_v1 &program, const void *frame) {
  ArgumentStorage storage;
  size_t count = static_cast<size_t>(array.size);
  storage.arguments.resize(count);
  storage.values.resize(count);
  storage.unknowns.resize(count);
  storage.reals.resize(count);
  storage.times.resize(count);
  for (size_t index = 0; index != count; ++index) {
    const auto &operand = program.operands[array.value + index];
    auto &argument = storage.arguments[index];
    switch (operand.value_kind) {
    case OBELISK_RT_BC_VALUE_ARGUMENT_EMPTY:
      argument = {OBELISK_RT_ARG_EMPTY, 0, 0, nullptr, nullptr};
      break;
    case OBELISK_RT_BC_VALUE_ARGUMENT_STRING: {
      const uint8_t *data = operandAddress(operand, program, frame);
      argument = {OBELISK_RT_ARG_STRING, operand.flags, operand.size, data,
                  nullptr};
      break;
    }
    case OBELISK_RT_BC_VALUE_ARGUMENT_LOGIC: {
      uint64_t wordCount = operand.size / 64 + (operand.size % 64 != 0);
      const uint8_t *valueBytes = operandAddress(operand, program, frame);
      storage.values[index].resize(static_cast<size_t>(wordCount));
      for (uint64_t word = 0; word != wordCount; ++word)
        storage.values[index][word] = readU64(valueBytes + word * 8);
      const uint64_t *unknown = nullptr;
      if (operand.auxiliary != UINT64_MAX) {
        const uint8_t *base = operand.kind == OBELISK_RT_BC_OPERAND_FRAME
                                  ? static_cast<const uint8_t *>(frame)
                                  : program.constants;
        storage.unknowns[index].resize(static_cast<size_t>(wordCount));
        for (uint64_t word = 0; word != wordCount; ++word)
          storage.unknowns[index][word] =
              readU64(base + operand.auxiliary + word * 8);
        unknown = storage.unknowns[index].data();
      }
      argument = {OBELISK_RT_ARG_LOGIC, operand.flags, operand.size,
                  storage.values[index].data(), unknown};
      break;
    }
    case OBELISK_RT_BC_VALUE_ARGUMENT_REAL: {
      uint64_t bits = readU64(operandAddress(operand, program, frame));
      std::memcpy(&storage.reals[index], &bits, sizeof(bits));
      argument = {OBELISK_RT_ARG_REAL, 0, 0, &storage.reals[index], nullptr};
      break;
    }
    case OBELISK_RT_BC_VALUE_ARGUMENT_TIME:
      storage.times[index] = readU64(operandAddress(operand, program, frame));
      argument = {OBELISK_RT_ARG_TIME, 0, 64, &storage.times[index], nullptr};
      break;
    default:
      return std::nullopt;
    }
  }
  return storage;
}

struct EnvironmentStorage {
  obelisk_rt_format_env_v1 environment{};
  bool present = false;
};

std::optional<EnvironmentStorage>
buildEnvironment(const obelisk_rt_bytecode_operand_v1 &operand,
                 const obelisk_rt_bytecode_v1 &program, const void *frame,
                 const RegisterFile &registers, ResourceTable &resources) {
  EnvironmentStorage storage;
  if (operand.size == 0)
    return storage;
  const auto *children = program.operands + operand.value;
  auto scope =
      readBytesOperand(children[0], program, frame, registers, resources);
  auto libraryCell =
      readBytesOperand(children[1], program, frame, registers, resources);
  auto timeWidth = readScalarOperand(children[2], program, frame, registers);
  auto suffix =
      readBytesOperand(children[3], program, frame, registers, resources);
  auto timeMultiplier =
      readScalarOperand(children[4], program, frame, registers);
  if (!scope || !libraryCell || !timeWidth || !suffix || !timeMultiplier ||
      *timeWidth > UINT32_MAX || *timeMultiplier == 0)
    return std::nullopt;
  storage.environment = {reinterpret_cast<const char *>(scope->data),
                         scope->size,
                         reinterpret_cast<const char *>(libraryCell->data),
                         libraryCell->size,
                         static_cast<uint32_t>(*timeWidth),
                         0,
                         reinterpret_cast<const char *>(suffix->data),
                         suffix->size,
                         *timeMultiplier};
  storage.present = true;
  return storage;
}

bool writeBufferOperand(const obelisk_rt_bytecode_operand_v1 &operand,
                        obelisk_rt_buffer_v1 buffer, RegisterFile &registers,
                        ResourceTable &resources) {
  uint16_t index = static_cast<uint16_t>(operand.value);
  if (registers.type(index) == OBELISK_RT_BC_TYPE_RESOURCE)
    return false;
  uint64_t id = resources.adopt(buffer);
  if (id == 0)
    return false;
  registers.define(index, OBELISK_RT_BC_TYPE_RESOURCE, id);
  return true;
}

std::optional<obelisk_rt_status>
invokeService(const obelisk_rt_bytecode_service_site_v1 &site,
              const obelisk_rt_bytecode_v1 &program,
              obelisk_rt_context *context, void *frame, RegisterFile &registers,
              ResourceTable &resources) {
  const auto *operand = program.operands + site.first_operand;
  // Validate result destinations before invoking a service. In particular,
  // do not perform an externally visible file operation and only then learn
  // that its result would overwrite a live transient resource.
  for (uint32_t index = 0; index != site.operand_count; ++index)
    if (operand[index].direction == OBELISK_RT_BC_OPERAND_OUTPUT &&
        operand[index].kind == OBELISK_RT_BC_OPERAND_REGISTER &&
        registers.type(static_cast<uint16_t>(operand[index].value)) ==
            OBELISK_RT_BC_TYPE_RESOURCE)
      return std::nullopt;
  auto scalar = [&](size_t index) {
    return readScalarOperand(operand[index], program, frame, registers);
  };
  auto bytes = [&](size_t index) {
    return readBytesOperand(operand[index], program, frame, registers,
                            resources);
  };
  auto writeScalar = [&](size_t index, uint64_t value) {
    return writeScalarOperand(operand[index], frame, registers, value);
  };
  auto arguments = [&](size_t index) {
    return buildArguments(operand[index], program, frame);
  };
  auto environment = [&](size_t index) {
    return buildEnvironment(operand[index], program, frame, registers,
                            resources);
  };
  obelisk_rt_status status = OBELISK_RT_INVALID_ARGUMENT;
  switch (site.service) {
  case OBELISK_RT_BC_SERVICE_FORMAT: {
    auto format = bytes(0);
    auto args = arguments(1);
    auto env = environment(2);
    if (!format || !args || !env)
      return std::nullopt;
    obelisk_rt_buffer_v1 output{};
    status = obelisk_rt_v1_format(
        context, reinterpret_cast<const char *>(format->data), format->size,
        args->arguments.data(), args->arguments.size(),
        env->present ? &env->environment : nullptr, &output);
    if (!writeBufferOperand(operand[3], output, registers, resources)) {
      obelisk_rt_v1_buffer_release(&output);
      return std::nullopt;
    }
    return status;
  }
  case OBELISK_RT_BC_SERVICE_DISPLAY: {
    auto descriptor = scalar(0), newline = scalar(1), radix = scalar(2);
    auto args = arguments(3);
    auto env = environment(4);
    if (!descriptor || !newline || !radix || !args || !env ||
        *descriptor > UINT32_MAX || *newline > UINT32_MAX ||
        *radix > UINT32_MAX)
      return std::nullopt;
    return obelisk_rt_v1_display(context, static_cast<uint32_t>(*descriptor),
                                 static_cast<uint32_t>(*newline),
                                 static_cast<obelisk_rt_radix>(*radix),
                                 args->arguments.data(), args->arguments.size(),
                                 env->present ? &env->environment : nullptr);
  }
  case OBELISK_RT_BC_SERVICE_BUFFER_RELEASE: {
    uint16_t index = static_cast<uint16_t>(operand[0].value);
    if (registers.type(index) != OBELISK_RT_BC_TYPE_RESOURCE ||
        !resources.release(registers.value(index)))
      return std::nullopt;
    registers.clear(index);
    return OBELISK_RT_OK;
  }
  case OBELISK_RT_BC_SERVICE_FILE_OPEN_MCD: {
    auto path = bytes(0);
    if (!path)
      return std::nullopt;
    uint32_t descriptor = 0;
    status = obelisk_rt_v1_file_open_mcd(
        context, reinterpret_cast<const char *>(path->data), path->size,
        &descriptor);
    if (!writeScalar(1, descriptor))
      return std::nullopt;
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_OPEN: {
    auto path = bytes(0), mode = bytes(1);
    if (!path || !mode)
      return std::nullopt;
    uint32_t descriptor = 0;
    status = obelisk_rt_v1_file_open(
        context, reinterpret_cast<const char *>(path->data), path->size,
        reinterpret_cast<const char *>(mode->data), mode->size, &descriptor);
    if (!writeScalar(2, descriptor))
      return std::nullopt;
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_CLOSE:
  case OBELISK_RT_BC_SERVICE_FILE_FLUSH:
  case OBELISK_RT_BC_SERVICE_FILE_REWIND: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return std::nullopt;
    if (site.service == OBELISK_RT_BC_SERVICE_FILE_CLOSE)
      return obelisk_rt_v1_file_close(context,
                                      static_cast<uint32_t>(*descriptor));
    if (site.service == OBELISK_RT_BC_SERVICE_FILE_FLUSH)
      return obelisk_rt_v1_file_flush(context,
                                      static_cast<uint32_t>(*descriptor));
    return obelisk_rt_v1_file_rewind(context,
                                     static_cast<uint32_t>(*descriptor));
  }
  case OBELISK_RT_BC_SERVICE_FILE_WRITE: {
    auto descriptor = scalar(0);
    auto data = bytes(1);
    if (!descriptor || *descriptor > UINT32_MAX || !data)
      return std::nullopt;
    uint64_t written = 0;
    status =
        obelisk_rt_v1_file_write(context, static_cast<uint32_t>(*descriptor),
                                 data->data, data->size, &written);
    if (!writeScalar(2, written))
      return std::nullopt;
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_READ: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return std::nullopt;
    void *data = operand[1].size == 0
                     ? nullptr
                     : static_cast<uint8_t *>(frame) + operand[1].value;
    uint64_t read = 0;
    status =
        obelisk_rt_v1_file_read(context, static_cast<uint32_t>(*descriptor),
                                data, operand[1].size, &read);
    if (!writeScalar(2, read))
      return std::nullopt;
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_GETC: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return std::nullopt;
    uint8_t byte = 0;
    status = obelisk_rt_v1_file_getc(context,
                                     static_cast<uint32_t>(*descriptor), &byte);
    if (!writeScalar(1, byte))
      return std::nullopt;
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_UNGETC: {
    auto descriptor = scalar(0), byte = scalar(1);
    if (!descriptor || !byte || *descriptor > UINT32_MAX || *byte > UINT8_MAX)
      return std::nullopt;
    return obelisk_rt_v1_file_ungetc(context,
                                     static_cast<uint32_t>(*descriptor),
                                     static_cast<uint8_t>(*byte));
  }
  case OBELISK_RT_BC_SERVICE_FILE_GETLINE: {
    auto descriptor = scalar(0), maxBytes = scalar(1);
    if (!descriptor || !maxBytes || *descriptor > UINT32_MAX)
      return std::nullopt;
    obelisk_rt_buffer_v1 output{};
    status = obelisk_rt_v1_file_getline(
        context, static_cast<uint32_t>(*descriptor), *maxBytes, &output);
    if (!writeBufferOperand(operand[2], output, registers, resources)) {
      obelisk_rt_v1_buffer_release(&output);
      return std::nullopt;
    }
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_EOF: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return std::nullopt;
    uint32_t eof = 0;
    status = obelisk_rt_v1_file_eof(context, static_cast<uint32_t>(*descriptor),
                                    &eof);
    if (!writeScalar(1, eof))
      return std::nullopt;
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_ERROR: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return std::nullopt;
    int32_t code = 0;
    obelisk_rt_buffer_v1 output{};
    status = obelisk_rt_v1_file_error(
        context, static_cast<uint32_t>(*descriptor), &code, &output);
    if (!writeScalar(1, static_cast<uint64_t>(static_cast<int64_t>(code))) ||
        !writeBufferOperand(operand[2], output, registers, resources)) {
      obelisk_rt_v1_buffer_release(&output);
      return std::nullopt;
    }
    return status;
  }
  case OBELISK_RT_BC_SERVICE_FILE_SEEK: {
    auto descriptor = scalar(0), offset = scalar(1), origin = scalar(2);
    if (!descriptor || !offset || !origin || *descriptor > UINT32_MAX ||
        *origin > UINT32_MAX)
      return std::nullopt;
    return obelisk_rt_v1_file_seek(
        context, static_cast<uint32_t>(*descriptor),
        static_cast<int64_t>(*offset),
        static_cast<obelisk_rt_seek_origin>(*origin));
  }
  case OBELISK_RT_BC_SERVICE_FILE_TELL: {
    auto descriptor = scalar(0);
    if (!descriptor || *descriptor > UINT32_MAX)
      return std::nullopt;
    int64_t offset = 0;
    status = obelisk_rt_v1_file_tell(
        context, static_cast<uint32_t>(*descriptor), &offset);
    if (!writeScalar(1, static_cast<uint64_t>(offset)))
      return std::nullopt;
    return status;
  }
  default:
    return std::nullopt;
  }
}

obelisk_rt_status executeBytecodeV1(const obelisk_rt_bytecode_v1 &program,
                                    obelisk_rt_context *context, void *frame,
                                    uint64_t frameSize, uint32_t continuation,
                                    uint64_t instructionLimit,
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
  ResourceTable resources(program.register_count);

  uint64_t pc = program.entries[low].instruction;
  uint64_t steps = 0;
  while (pc < instructionCount) {
    if (instructionLimit != 0 && ++steps > instructionLimit)
      return OBELISK_RT_STEP_LIMIT;
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
      if (!registers.contains(destination) ||
          (!isScalarType(resultType) &&
           resultType != OBELISK_RT_BC_TYPE_STATUS) ||
          registers.type(destination) == OBELISK_RT_BC_TYPE_RESOURCE)
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
          (type == OBELISK_RT_BC_TYPE_STATUS &&
           immediate > OBELISK_RT_STEP_LIMIT) ||
          (!isScalarType(type) && type != OBELISK_RT_BC_TYPE_STATUS) ||
          !define(immediate, type))
        return OBELISK_RT_INVALID_BYTECODE;
      break;
    case OBELISK_RT_BC_MOVE:
      if (!(validSource(source0, type, registers) ||
            (type == OBELISK_RT_BC_TYPE_STATUS && registers.contains(source0) &&
             registers.type(source0) == OBELISK_RT_BC_TYPE_STATUS)) ||
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
      if (type == OBELISK_RT_BC_TYPE_STATUS) {
        if (!registers.contains(source0) || !registers.contains(source1) ||
            registers.type(source0) != OBELISK_RT_BC_TYPE_STATUS ||
            registers.type(source1) != OBELISK_RT_BC_TYPE_STATUS ||
            !define(registers.value(source0) == registers.value(source1),
                    OBELISK_RT_BC_TYPE_BOOL))
          return OBELISK_RT_INVALID_BYTECODE;
      } else if (!binary([](uint64_t lhs, uint64_t rhs) { return lhs == rhs; },
                         OBELISK_RT_BC_TYPE_BOOL)) {
        return OBELISK_RT_INVALID_BYTECODE;
      }
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
      if (!(validSource(source0, type, registers) ||
            (type == OBELISK_RT_BC_TYPE_STATUS && registers.contains(source0) &&
             registers.type(source0) == OBELISK_RT_BC_TYPE_STATUS)) ||
          immediate >= instructionCount)
        return OBELISK_RT_INVALID_BYTECODE;
      if (registers.value(source0) == 0)
        pc = immediate;
      break;
    case OBELISK_RT_BC_CONTINUE:
      if (type != OBELISK_RT_BC_TYPE_NONE || immediate > UINT32_MAX)
        return OBELISK_RT_INVALID_BYTECODE;
      if (resources.hasLiveResources())
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
          source0 > OBELISK_RT_SUSPEND_CHILDREN || immediate > UINT32_MAX ||
          (source1 != UINT16_MAX &&
           !validSource(source1, OBELISK_RT_BC_TYPE_U64, registers)))
        return OBELISK_RT_INVALID_BYTECODE;
      if (resources.hasLiveResources())
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
      if (resources.hasLiveResources())
        return OBELISK_RT_INVALID_BYTECODE;
      *action = {OBELISK_RT_FRAGMENT_TERMINATE,
                 OBELISK_RT_SUSPEND_NONE,
                 0,
                 0,
                 immediate,
                 0};
      return OBELISK_RT_OK;
    case OBELISK_RT_BC_CALL_SERVICE: {
      if (type != OBELISK_RT_BC_TYPE_STATUS ||
          !registers.contains(destination) || source0 != 0 || source1 != 0 ||
          immediate >= program.service_site_count ||
          registers.type(destination) == OBELISK_RT_BC_TYPE_RESOURCE)
        return OBELISK_RT_INVALID_BYTECODE;
      std::optional<obelisk_rt_status> status =
          invokeService(program.service_sites[immediate], program, context,
                        frame, registers, resources);
      if (!status)
        return OBELISK_RT_INVALID_BYTECODE;
      if (registers.type(destination) == OBELISK_RT_BC_TYPE_RESOURCE)
        return OBELISK_RT_INVALID_BYTECODE;
      registers.define(destination, OBELISK_RT_BC_TYPE_STATUS,
                       static_cast<uint64_t>(static_cast<int64_t>(*status)));
      break;
    }
    case OBELISK_RT_BC_FAIL: {
      if (type != OBELISK_RT_BC_TYPE_STATUS || destination != 0 ||
          !registers.contains(source0) ||
          registers.type(source0) != OBELISK_RT_BC_TYPE_STATUS ||
          source1 != 0 || immediate != 0)
        return OBELISK_RT_INVALID_BYTECODE;
      int64_t value = static_cast<int64_t>(registers.value(source0));
      if (value <= OBELISK_RT_OK || value > OBELISK_RT_STEP_LIMIT)
        return OBELISK_RT_INVALID_BYTECODE;
      // ResourceTable releases every live transient on this error path.
      return static_cast<obelisk_rt_status>(value);
    }
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
           action.suspend_kind <= OBELISK_RT_SUSPEND_SEMAPHORE;
  case OBELISK_RT_FRAGMENT_TERMINATE:
    return action.suspend_kind == OBELISK_RT_SUSPEND_NONE &&
           action.continuation == 0 && action.auxiliary == 0;
  default:
    return false;
  }
}

obelisk_rt_status
executeFragment(const obelisk_rt_fragment_descriptor_v1 *descriptor,
                obelisk_rt_context *context, void *frame, uint64_t frameSize,
                uint32_t continuation, uint64_t instructionLimit,
                bool bytecodeOnly, obelisk_rt_fragment_action_v1 *outAction) {
  if (!descriptor || !outAction ||
      descriptor->handle.kind != OBELISK_RT_DESCRIPTOR_FRAGMENT ||
      (frameSize != 0 && !frame))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outAction = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  if (descriptor->flags != OBELISK_RT_FRAGMENT_FLAGS_NONE)
    return OBELISK_RT_INVALID_ARGUMENT;
  ManagedExecutionScope managedExecution(context);
  if (managedExecution.getStatus() != OBELISK_RT_OK)
    return managedExecution.getStatus();
  obelisk_rt_status status;
  if (descriptor->code_kind == OBELISK_RT_FRAGMENT_NATIVE) {
    if (bytecodeOnly || !descriptor->code.native_entry)
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
    try {
      status = executeBytecodeV1(descriptor->code.bytecode, context, frame,
                                 frameSize, continuation, instructionLimit,
                                 outAction);
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (...) {
      return OBELISK_RT_INVALID_BYTECODE;
    }
  } else {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  if (status != OBELISK_RT_OK)
    return status;
  return validAction(*outAction) ? OBELISK_RT_OK : OBELISK_RT_INVALID_ARGUMENT;
}

} // namespace

obelisk_rt_status
obelisk_rt_validate_bytecode_program(const obelisk_rt_bytecode_v1 &program,
                                     uint32_t continuation) noexcept {
  try {
    if (!program.code || program.code_size == 0 ||
        program.code_size % OBELISK_RT_BYTECODE_INSTRUCTION_SIZE != 0 ||
        program.register_count > static_cast<uint32_t>(UINT16_MAX) + 1u ||
        program.entry_count == 0 || !program.entries)
      return OBELISK_RT_INVALID_BYTECODE;
    const uint64_t instructionCount =
        program.code_size / OBELISK_RT_BYTECODE_INSTRUCTION_SIZE;
    if (!ensureEntryTableValidated(program, instructionCount))
      return OBELISK_RT_INVALID_BYTECODE;
    auto *begin = program.entries;
    auto *end = begin + program.entry_count;
    auto *entry = std::lower_bound(
        begin, end, continuation,
        [](const obelisk_rt_bytecode_entry_v1 &candidate, uint32_t id) {
          return candidate.continuation < id;
        });
    return entry != end && entry->continuation == continuation
               ? OBELISK_RT_OK
               : OBELISK_RT_TIER_UNAVAILABLE;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_BYTECODE;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_fragment_execute(
    const obelisk_rt_fragment_descriptor_v1 *descriptor,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint32_t continuation, obelisk_rt_fragment_action_v1 *outAction) {
  return executeFragment(descriptor, context, frame, frameSize, continuation, 0,
                         false, outAction);
}

extern "C" obelisk_rt_status obelisk_rt_v1_bytecode_execute_bounded(
    const obelisk_rt_fragment_descriptor_v1 *descriptor,
    obelisk_rt_context *context, void *frame, uint64_t frameSize,
    uint32_t continuation, uint64_t instructionLimit,
    obelisk_rt_fragment_action_v1 *outAction) {
  if (instructionLimit == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  return executeFragment(descriptor, context, frame, frameSize, continuation,
                         instructionLimit, true, outAction);
}
